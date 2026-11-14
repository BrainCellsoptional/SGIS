#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <opencv2/opencv.hpp>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <regex>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <future>

// Shared aliases and server-wide configuration values.
using json = nlohmann::json;
namespace fs = std::filesystem;
static constexpr int PORT = 3000;
static constexpr long long SESSION_TTL = 8 * 60 * 60;
static constexpr size_t MAX_UPLOAD = 150ULL * 1024 * 1024;

static fs::path dataDir() {
    const char* p = std::getenv("SGIS_DATA_DIR");
    return p ? fs::path(p) : fs::path("/data");
}
static fs::path dbPath() { return dataDir() / "sgis.db"; }
static fs::path imageDir() { return dataDir() / "sgis_images"; }

// SQLite wrapper: owns the database connection and provides helpers for
// prepared statements, schema checks, and SQL execution.
struct DB {
    sqlite3* db{};
    struct Stmt {
        sqlite3_stmt* st{};
        Stmt(sqlite3* d,const std::string& q){ if(sqlite3_prepare_v2(d,q.c_str(),-1,&st,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(d)); }
        ~Stmt(){ if(st) sqlite3_finalize(st); }
        void text(int i,const std::string& v){ sqlite3_bind_text(st,i,v.c_str(),-1,SQLITE_TRANSIENT); }
        void integer(int i,long long v){ sqlite3_bind_int64(st,i,v); }
        void real(int i,double v){ sqlite3_bind_double(st,i,v); }
        bool step(){ int r=sqlite3_step(st); if(r==SQLITE_ROW)return true; if(r==SQLITE_DONE)return false; throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(st))); }
        std::string str(int i){ auto p=sqlite3_column_text(st,i); return p?(const char*)p:""; }
        long long num(int i){ return sqlite3_column_int64(st,i); }
        double dbl(int i){ return sqlite3_column_double(st,i); }
        bool isNull(int i){ return sqlite3_column_type(st,i)==SQLITE_NULL; }
    };
    DB() {
        fs::create_directories(dataDir());
        fs::create_directories(imageDir());
        if (sqlite3_open(dbPath().c_str(), &db) != SQLITE_OK)
            throw std::runtime_error("Could not open SQLite database");
        sqlite3_busy_timeout(db, 5000);
        exec(R"SQL(
          CREATE TABLE IF NOT EXISTS images (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT,
            file_path TEXT,
            notes TEXT,
            long TEXT,
            lat TEXT
          );
          CREATE TABLE IF NOT EXISTS settings (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            water INTEGER,
            vegetation INTEGER,
            dirt INTEGER,
            noise INTEGER,
            parallel INTEGER,
            shadows INTEGER,
            lat REAL,
            lng REAL,
            rocks INTEGER NOT NULL DEFAULT 1
          );
          CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL COLLATE NOCASE UNIQUE,
            password_hash TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
          );
          CREATE TABLE IF NOT EXISTS sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            token_hash TEXT NOT NULL UNIQUE,
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            expires_at INTEGER NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
          );
          CREATE INDEX IF NOT EXISTS idx_sessions_expires_at ON sessions(expires_at);
          CREATE TABLE IF NOT EXISTS quick_locations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL COLLATE NOCASE UNIQUE,
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
          );
        )SQL");
        exec("INSERT OR IGNORE INTO settings "
             "(id,water,vegetation,dirt,noise,parallel,shadows,lat,lng,rocks) "
             "VALUES (1,1,1,1,1,1,1,-45.510314,169.7843164,1)");
        if (hasTable("settings") && !hasColumn("settings", "rocks"))
            exec("ALTER TABLE settings ADD COLUMN rocks INTEGER NOT NULL DEFAULT 1");
    }
    ~DB(){ if(db) sqlite3_close(db); }
    void exec(const std::string& q) {
        char* err=nullptr;
        if(sqlite3_exec(db,q.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
            std::string e=err?err:"SQLite error"; sqlite3_free(err); throw std::runtime_error(e);
        }
    }
    bool hasTable(const std::string& t) {
        Stmt s(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
        s.text(1,t); return s.step();
    }
    bool hasColumn(const std::string& table,const std::string& col) {
        Stmt s(db,"PRAGMA table_info("+table+")");
        while(s.step()) if(s.str(1)==col) return true;
        return false;
    }
};
// Shared database handle and mutexes protect state accessed by request/job threads.
static DB* gdb=nullptr;
static std::mutex dbMutex, jobMutex, rateMutex;
static std::unordered_map<std::string,std::pair<int,long long>> loginRates, processRates;

// Tracks one background image-processing task and the information
// returned by the progress, result, and cancellation API endpoints.
struct ProcessingJob {
    std::string status = "processing";
    std::string error;
    fs::path directory, output;
    pid_t pid = -1;
    int percent = 0;
    std::string stage = "Starting processor";
    int current = 0, total = 0;
    bool ready = false;
    std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
};
static std::unordered_map<std::string,std::shared_ptr<ProcessingJob>> processingJobs;
static std::mutex processingMutex;
// Progress state for an area-image scan, separate from single-image jobs.
struct AreaProgress { int percent=0,current=0,total=0; std::string stage="Starting Area Scan",error; bool ready=false; };
static std::unordered_map<std::string,AreaProgress> areaJobs;
static std::mutex areaMutex;

// Reject malformed job identifiers before looking up background-job state.
static bool validJobId(const std::string& id) {
    return std::regex_match(id, std::regex("[A-Za-z0-9-]{1,100}"));
}
// Update a job while holding its mutex so worker and HTTP threads stay in sync.
static void updateJob(const std::string& id, const std::function<void(ProcessingJob&)>& fn) {
    std::lock_guard<std::mutex> lock(processingMutex);
    auto it = processingJobs.find(id);
    if (it != processingJobs.end()) fn(*it->second);
}
// Format the current job state as a Server-Sent Events message.
static std::string sseProgress(const ProcessingJob& j) {
    json event = {{"percent",j.percent},{"stage",j.stage},{"current",j.current},{"total",j.total}};
    if (j.ready) event["ready"] = true;
    if (!j.error.empty()) event["error"] = j.error;
    return "data: " + event.dump() + "\n\n";
}
// Start the external OpenCV processor and read its SGIS_PROGRESS output
// so the API can report processing stages to the frontend.
static void launchProcessor(const std::string& id, const fs::path& input,
                            const fs::path& output, const std::vector<std::string>& flags) {
    auto executable = std::getenv("SGIS_PROCESSOR");
    fs::path exe = executable ? fs::path(executable) : fs::path("/app/processor/sgis-process");
    std::thread([id,input,output,flags,exe]() {
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            updateJob(id,[](auto& j){j.status="failed";j.error="Could not create processor output pipe";});
            return;
        }
        pid_t pid = fork();
        if (pid == 0) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]); close(pipefd[1]);
            std::vector<std::string> args;
            args.push_back(exe.string()); args.push_back(input.string()); args.push_back(output.string());
            args.insert(args.end(),flags.begin(),flags.end());
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(a.data());
            argv.push_back(nullptr);
            execv(exe.c_str(),argv.data());
            _exit(127);
        }
        close(pipefd[1]);
        if (pid < 0) {
            close(pipefd[0]);
            updateJob(id,[](auto& j){j.status="failed";j.error="Could not start image processor";});
            return;
        }
        updateJob(id,[&](auto& j){j.pid=pid;});
        std::string buffer;
        char chunk[2048];
        while (true) {
            ssize_t n=read(pipefd[0],chunk,sizeof(chunk));
            if(n<=0) break;
            buffer.append(chunk,(size_t)n);
            size_t pos;
            while((pos=buffer.find('\n'))!=std::string::npos) {
                std::string line=buffer.substr(0,pos); buffer.erase(0,pos+1);
                if(line.rfind("SGIS_PROGRESS|",0)!=0) continue;
                std::stringstream ss(line); std::string part;
                std::getline(ss,part,'|'); std::getline(ss,part,'|');
                int pct=0; try{pct=std::stoi(part);}catch(...){continue;}
                std::string stage,cur,total;
                std::getline(ss,stage,'|'); std::getline(ss,cur,'|'); std::getline(ss,total,'|');
                int c=0,t=0; try{c=std::stoi(cur);}catch(...){} try{t=std::stoi(total);}catch(...){}
                updateJob(id,[&](auto& j){j.percent=std::clamp(pct,0,100);j.stage=stage;j.current=c;j.total=t;});
            }
        }
        close(pipefd[0]);
        int status=0; waitpid(pid,&status,0);
        updateJob(id,[&](auto& j){
            j.pid=-1;
            if(j.status=="cancelled") return;
            if(WIFEXITED(status) && WEXITSTATUS(status)==0 && fs::exists(output)) {
                j.status="completed";j.percent=100;j.stage="Image ready";j.ready=true;
            } else {
                j.status="failed";j.error="Image processor failed (exit code "+std::to_string(WIFEXITED(status)?WEXITSTATUS(status):-1)+")";
            }
        });
    }).detach();
}


// Authentication/session helpers: time, token encoding, hashing, and password checks.
static long long nowSec(){ return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
static std::string hex(const unsigned char* p,size_t n){ static const char* h="0123456789abcdef"; std::string o; o.reserve(n*2); for(size_t i=0;i<n;i++){o+=h[p[i]>>4];o+=h[p[i]&15];} return o; }
static std::string randomHex(size_t n){ std::vector<unsigned char>b(n); if(RAND_bytes(b.data(),(int)n)!=1) throw std::runtime_error("Random generator failed"); return hex(b.data(),n); }
static std::string sha256(const std::string& s){ unsigned char d[SHA256_DIGEST_LENGTH]; SHA256((const unsigned char*)s.data(),s.size(),d); return hex(d,sizeof d); }
static bool scryptVerify(const std::string& password,const std::string& stored){
    // Compatible with Node.js scryptSync(password, salt, 64, N=32768,r=8,p=1).
    std::stringstream ss(stored); std::string scheme,salt,want;
    if(!std::getline(ss,scheme,'$')||!std::getline(ss,salt,'$')||!std::getline(ss,want,'$')||scheme!="scrypt"||want.size()!=128)return false;
    std::vector<unsigned char> out(64);
    if(EVP_PBE_scrypt(password.c_str(),password.size(),reinterpret_cast<const unsigned char*>(salt.data()),salt.size(),32768,8,1,64*1024*1024,out.data(),out.size())!=1)return false;
    return CRYPTO_memcmp(out.data(),[] (const std::string& x) {
        static thread_local std::vector<unsigned char> b(64);
        for(size_t i=0;i<64;i++){ auto cv=[](char c)->int { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; return -1;}; int a=cv(x[i*2]),z=cv(x[i*2+1]); if(a<0||z<0){b[i]=0;}else b[i]=(a<<4)|z; }
        return b.data();
    }(want),64)==0;
}
// Common HTTP response helpers keep JSON and security headers consistent.
static void jsonOut(httplib::Response& r,const json& j,int status=200){ r.status=status; r.set_content(j.dump(),"application/json"); }
static void securityHeaders(httplib::Response& r){
    r.set_header("X-Content-Type-Options","nosniff");
    r.set_header("Referrer-Policy","strict-origin-when-cross-origin");
    r.set_header("X-Frame-Options","DENY");
    r.set_header("Permissions-Policy","camera=(), microphone=(), geolocation=()");
}
static std::string cookie(const httplib::Request& req,const std::string& name){
    if(!req.has_header("Cookie"))return "";
    std::string all=req.get_header_value("Cookie"), key=name+"=";
    size_t p=all.find(key); if(p==std::string::npos)return "";
    p+=key.size(); size_t e=all.find(';',p); return all.substr(p,e==std::string::npos?e:e-p);
}
// Minimal user identity loaded from a valid session.
struct User{ long long id; std::string username; };
// Validate the session cookie against the hashed token stored in SQLite.
static bool auth(const httplib::Request& req,User& u){
    std::string token=cookie(req,"sgis_session");
    if(!std::regex_match(token,std::regex("[a-f0-9]{64}")))return false;
    std::lock_guard<std::mutex> lk(dbMutex);
    DB::Stmt q(gdb->db,"SELECT users.id,users.username,sessions.expires_at FROM sessions JOIN users ON users.id=sessions.user_id WHERE sessions.token_hash=?");
    q.text(1,sha256(token));
    if(!q.step())return false;
    if(q.num(2)<=nowSec()){ DB::Stmt d(gdb->db,"DELETE FROM sessions WHERE token_hash=?"); d.text(1,sha256(token)); d.step(); return false; }
    u={q.num(0),q.str(1)}; return true;
}
// Shared guard used by endpoints that already require a signed-in user.
static bool requireAuth(const httplib::Request& req,httplib::Response& res){
    User u; if(auth(req,u))return true;
    jsonOut(res,{{"success",false},{"error","Login required."}},401); return false;
}
static bool validJob(const std::string& id){return std::regex_match(id,std::regex("[A-Za-z0-9-]{1,100}"));}
static bool validUsername(const std::string& s){return std::regex_match(s,std::regex("[A-Za-z0-9._-]{3,40}"));}
static std::string ipKey(const httplib::Request& req){ return req.remote_addr.empty()?"unknown":req.remote_addr; }
static bool allowRate(std::unordered_map<std::string,std::pair<int,long long>>& m,const std::string& key,int limit,int window){
    std::lock_guard<std::mutex> l(rateMutex); auto now=nowSec(); auto& x=m[key];
    if(x.second==0||now>=x.second){x={0,now+window};}
    if(x.first>=limit)return false; ++x.first; return true;
}
static json bodyJson(const httplib::Request& req){ try{return json::parse(req.body);}catch(...){return json::object();} }
static bool safeImage(const std::string& bytes,cv::Mat& image){
    if(bytes.empty()||bytes.size()>MAX_UPLOAD)return false;
    std::vector<unsigned char> b(bytes.begin(),bytes.end());
    image=cv::imdecode(b,cv::IMREAD_UNCHANGED);
    if(image.empty()||image.cols<=0||image.rows<=0||(long long)image.cols*image.rows>150000000LL)return false;
    return image.channels()==1||image.channels()==3||image.channels()==4;
}
static std::string saveJpeg(const cv::Mat& img,const fs::path& path,int quality=90){
    std::vector<unsigned char> out; if(!cv::imencode(".jpg",img,out,{cv::IMWRITE_JPEG_QUALITY,quality}))throw std::runtime_error("Image conversion failed");
    std::ofstream f(path,std::ios::binary); f.write((char*)out.data(),out.size()); if(!f)throw std::runtime_error("Could not save image");
    return std::string(out.begin(),out.end());
}
static bool safeStoredPath(const std::string& rel,fs::path& out){
    fs::path p=fs::weakly_canonical(dataDir()/rel), root=fs::weakly_canonical(imageDir());
    auto a=p.string(),b=root.string()+fs::path::preferred_separator;
    if(a.rfind(b,0)!=0)return false; out=p; return true;
}
static std::string jsonBool(bool b){return b?"true":"false";}


static size_t curlWrite(void* ptr,size_t size,size_t nmemb,void* userdata) {
    auto* out=static_cast<std::vector<unsigned char>*>(userdata);
    size_t n=size*nmemb; auto* p=static_cast<unsigned char*>(ptr);
    out->insert(out->end(),p,p+n); return n;
}
static std::vector<unsigned char> downloadTile(const std::string& url) {
    CURL* curl=curl_easy_init();
    if(!curl) throw std::runtime_error("Could not initialise LINZ download");
    std::vector<unsigned char> bytes;
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,20L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"SGIS/1.0");
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,curlWrite);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&bytes);
    CURLcode result=curl_easy_perform(curl);
    long status=0; curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_easy_cleanup(curl);
    if(result!=CURLE_OK) throw std::runtime_error(curl_easy_strerror(result));
    if(status<200||status>=300) throw std::runtime_error("LINZ tile HTTP "+std::to_string(status));
    if(bytes.empty()) throw std::runtime_error("LINZ returned an empty tile");
    return bytes;
}
static double tileXPos(double lon,int z) { return (lon+180.0)/360.0*std::pow(2.0,z); }
static double tileYPos(double lat,int z) {
    double r=lat*M_PI/180.0;
    return (1.0-std::asinh(std::tan(r))/M_PI)/2.0*std::pow(2.0,z);
}
static void updateArea(const std::string& id,const AreaProgress& value) {
    std::lock_guard<std::mutex> lk(areaMutex); areaJobs[id]=value;
}

// Starts the HTTP server, initialises shared resources, and registers the API routes.
// Server entry point: initialise storage/database, register API routes, then listen.
int main(){
 try{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    DB db; gdb=&db;
    httplib::Server app;
    app.set_payload_max_length(MAX_UPLOAD+1024*1024);
    app.set_pre_routing_handler([](const httplib::Request&,httplib::Response& res){securityHeaders(res); return httplib::Server::HandlerResponse::Unhandled;});
    app.set_error_handler([](const httplib::Request&,httplib::Response& res){ if(res.body.empty())jsonOut(res,{{"success",false},{"error","Request failed."}},res.status?res.status:404); });

    // Authentication endpoints: report the current session, create a session, and end it.
    // Authentication endpoints expose the current session and login/logout actions.
    app.Get("/auth/me",[](const httplib::Request& req,httplib::Response& res){
        User u; bool ok=auth(req,u); jsonOut(res,{{"authenticated",ok},{"user",ok?json{{"id",u.id},{"username",u.username}}:json(nullptr)}});
    });
    app.Post("/auth/login",[](const httplib::Request& req,httplib::Response& res){
        if(!allowRate(loginRates,ipKey(req),10,900)){jsonOut(res,{{"success",false},{"error","Too many login attempts. Try again in 15 minutes."}},429);return;}
        auto b=bodyJson(req); std::string name=b.value("username",std::string()),pw=b.value("password",std::string());
        while(!name.empty()&&std::isspace((unsigned char)name.back()))name.pop_back();
        if(!validUsername(name)||pw.empty()||pw.size()>200){jsonOut(res,{{"success",false},{"error","Invalid username or password."}},400);return;}
        std::lock_guard<std::mutex> lk(dbMutex);
        DB::Stmt q(gdb->db,"SELECT id,username,password_hash FROM users WHERE username=?"); q.text(1,name);
        if(!q.step()||!scryptVerify(pw,q.str(2))){jsonOut(res,{{"success",false},{"error","Invalid username or password."}},401);return;}
        long long uid=q.num(0); std::string uname=q.str(1),token=randomHex(32); long long exp=nowSec()+SESSION_TTL;
        DB::Stmt ins(gdb->db,"INSERT INTO sessions(token_hash,user_id,expires_at) VALUES(?,?,?)"); ins.text(1,sha256(token));ins.integer(2,uid);ins.integer(3,exp);ins.step();
        gdb->exec("DELETE FROM sessions WHERE expires_at <= "+std::to_string(nowSec()));
        std::string ck="sgis_session="+token+"; Path=/; HttpOnly; SameSite=Strict; Max-Age="+std::to_string(SESSION_TTL);
        if(req.get_header_value("X-Forwarded-Proto")=="https")ck+="; Secure";
        res.set_header("Set-Cookie",ck); jsonOut(res,{{"success",true},{"user",{{"id",uid},{"username",uname}}}});
    });
    app.Post("/auth/logout",[](const httplib::Request& req,httplib::Response& res){
        auto token=cookie(req,"sgis_session"); if(std::regex_match(token,std::regex("[a-f0-9]{64}"))){std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt d(gdb->db,"DELETE FROM sessions WHERE token_hash=?");d.text(1,sha256(token));d.step();}
        std::string ck="sgis_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0"; if(req.get_header_value("X-Forwarded-Proto")=="https")ck+="; Secure";res.set_header("Set-Cookie",ck);jsonOut(res,{{"success",true}});
    });

    // Quick-location endpoints used to save and retrieve frequently used map positions.
    // Quick locations are the saved map locations.
    app.Get("/quick_locations",[](const httplib::Request&,httplib::Response& res){
        std::lock_guard<std::mutex>lk(dbMutex); DB::Stmt q(gdb->db,"SELECT id,name,latitude,longitude FROM quick_locations ORDER BY name COLLATE NOCASE");json a=json::array();while(q.step())a.push_back({{"id",q.num(0)},{"name",q.str(1)},{"latitude",q.dbl(2)},{"longitude",q.dbl(3)}});jsonOut(res,a);
    });
    app.Post("/quick_locations",[](const httplib::Request& req,httplib::Response& res){
        if(!requireAuth(req,res))return;auto b=bodyJson(req);std::string name=b.value("name",std::string());double lat=b.value("latitude",999.0),lon=b.value("longitude",999.0);
        if(name.empty()||name.size()>60||lat < -90||lat>90||lon < -180||lon>180){jsonOut(res,{{"success",false},{"error","Invalid quick location details"}},400);return;}
        try{std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"INSERT INTO quick_locations(name,latitude,longitude) VALUES(?,?,?)");q.text(1,name);q.real(2,lat);q.real(3,lon);q.step();jsonOut(res,{{"success",true},{"id",sqlite3_last_insert_rowid(gdb->db)},{"name",name},{"latitude",lat},{"longitude",lon}},201);}catch(...){jsonOut(res,{{"success",false},{"error","A location with that name already exists"}},409);}
    });
    app.Delete(R"(/quick_locations/([0-9]+))",[](const httplib::Request& req,httplib::Response& res){
        if(!requireAuth(req,res))return;long long id=std::stoll(req.matches[1]);std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"DELETE FROM quick_locations WHERE id=?");q.integer(1,id);q.step();jsonOut(res,{{"success",sqlite3_changes(gdb->db)>0}});
    });

    // Settings endpoints store and return the image-processing options and map centre.
    // Read and update application processing/display settings.
    app.Get("/settings",[](const httplib::Request&,httplib::Response& res){
        std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"SELECT water,vegetation,dirt,noise,parallel,shadows,rocks,lat,lng FROM settings WHERE id=1");
        if(!q.step()){jsonOut(res,{{"error","Settings not found"}},404);return;}
        jsonOut(res,{{"water",q.num(0)!=0},{"vegetation",q.num(1)!=0},{"dirt",q.num(2)!=0},{"noise",q.num(3)!=0},{"parallel",q.num(4)!=0},{"shadows",q.num(5)!=0},{"rocks",q.num(6)!=0},{"lat",q.dbl(7)},{"lng",q.dbl(8)},{"linzUrl","https://basemaps.linz.govt.nz/v1/tiles/otago-2025-0.2m/WebMercatorQuad/{z}/{x}/{y}.webp?api=c01ksp9a9c1aweex6jqa4dr3raw"}});
    });
    app.Post("/settings",[](const httplib::Request& req,httplib::Response& res){
        if(!requireAuth(req,res))return;auto b=bodyJson(req);double lat=b.value("lat",999.0),lng=b.value("lng",999.0);
        const char* keys[]={"water","vegetation","dirt","noise","parallel","shadows","rocks"};
        if(lat < -90||lat>90||lng < -180||lng>180){jsonOut(res,{{"success",false},{"error","Invalid settings values."}},400);return;}
        for(auto k:keys)if(!b.contains(k)||!b[k].is_boolean()){jsonOut(res,{{"success",false},{"error","Invalid settings values."}},400);return;}
        std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"UPDATE settings SET water=?,vegetation=?,dirt=?,noise=?,parallel=?,shadows=?,rocks=?,lat=?,lng=? WHERE id=1");
        for(int i=0;i<7;i++)sqlite3_bind_int(q.st,i+1,b[keys[i]].get<bool>()?1:0);q.real(8,lat);q.real(9,lng);q.step();jsonOut(res,{{"success",sqlite3_changes(gdb->db)>0}});
    });

    // Image database endpoints list records, serve stored files, and manage uploads/metadata.
    // Image library endpoints list records, serve stored files, and manage uploads.
    app.Get("/images",[](const httplib::Request&,httplib::Response& res){
        std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"SELECT id,title,file_path,notes FROM images");json a=json::array();
        while(q.step())a.push_back({{"id",q.num(0)},{"title",q.str(1)},{"file_url","https://api.sgisonline.com/file/"+q.str(2)},{"notes",q.str(3)}});jsonOut(res,a);
    });
    app.Get(R"(/file/(.+))",[](const httplib::Request& req,httplib::Response& res){
        fs::path p;if(!safeStoredPath(req.matches[1],p)||!fs::exists(p)){res.status=404;res.set_content("File not found","text/plain");return;}
        std::ifstream f(p,std::ios::binary);std::string data((std::istreambuf_iterator<char>(f)),{});res.set_content(data,"image/jpeg");
    });
    app.Post("/upload",[](const httplib::Request& req,httplib::Response& res){
        if(!requireAuth(req,res))return;
        if(!req.has_file("image")){jsonOut(res,{{"success",false},{"error","No image file was uploaded"}},400);return;}
        auto file=req.get_file_value("image");cv::Mat img;if(!safeImage(file.content,img)){jsonOut(res,{{"success",false},{"error","The uploaded file is not a supported, valid image."}},400);return;}
        std::string t;
        std::string notes;
        if(req.has_file("title")) t=req.get_file_value("title").content;
        else if(req.has_param("title")) t=req.get_param_value("title");
        if(req.has_file("notes")) notes=req.get_file_value("notes").content;
        else if(req.has_param("notes")) notes=req.get_param_value("notes");
        if(t.empty()||t.size()>120||notes.size()>2000){jsonOut(res,{{"success",false},{"error","A title is required (maximum 120 characters); notes must be 2000 characters or fewer."}},400);return;}
        std::string fname=std::to_string(nowSec())+"_"+randomHex(8)+".jpg";fs::path path=imageDir()/fname;
        try{saveJpeg(img,path,90);std::string rel="sgis_images/"+fname;std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"INSERT INTO images(title,file_path,notes) VALUES(?,?,?)");q.text(1,t);q.text(2,rel);q.text(3,notes);q.step();jsonOut(res,{{"success",true},{"id",sqlite3_last_insert_rowid(gdb->db)},{"title",t},{"file_path",rel},{"notes",notes}},201);}catch(...){fs::remove(path);jsonOut(res,{{"success",false},{"error","Failed to save image record"}},500);}
    });
    app.Post("/update_image",[](const httplib::Request& req,httplib::Response& res){
        if(!requireAuth(req,res))return;auto b=bodyJson(req);long long id=b.value("id",0LL);std::string title=b.value("title",std::string()),notes=b.value("notes",std::string());
        if(id<=0||title.empty()||title.size()>120||notes.size()>2000){jsonOut(res,{{"error","A valid image ID and title are required; title maximum is 120 characters and notes maximum is 2000."}},400);return;}
        std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"UPDATE images SET title=?,notes=? WHERE id=?");q.text(1,title);q.text(2,notes);q.integer(3,id);q.step();jsonOut(res,{{"success",sqlite3_changes(gdb->db)>0}});
    });
    app.Post("/delete_image",[](const httplib::Request& req,httplib::Response& res){
        if(!requireAuth(req,res))return;auto b=bodyJson(req);long long id=b.value("id",0LL);if(id<=0){jsonOut(res,{{"error","Invalid image ID"}},400);return;}
        std::string rel;{std::lock_guard<std::mutex>lk(dbMutex);DB::Stmt q(gdb->db,"SELECT file_path FROM images WHERE id=?");q.integer(1,id);if(!q.step()){jsonOut(res,{{"success",false}});return;}rel=q.str(0);DB::Stmt d(gdb->db,"DELETE FROM images WHERE id=?");d.integer(1,id);d.step();}
        fs::path p;if(safeStoredPath(rel,p))fs::remove(p);jsonOut(res,{{"success",true}});
    });

    // Image processing job endpoints. The OpenCV algorithm runs in a separate executable.
    // The browser can subscribe to progress updates and retrieve or cancel a completed job.
    // Single-image processing endpoints start jobs and report progress/results.
    app.Get(R"(/process-progress/([A-Za-z0-9-]{1,100}))",[](const httplib::Request& req,httplib::Response& res){
        const std::string id=req.matches[1];
        res.set_header("Cache-Control","no-cache, no-transform");
        res.set_header("X-Accel-Buffering","no");
        res.set_chunked_content_provider("text/event-stream",
          [id](size_t,httplib::DataSink& sink){
            ProcessingJob snapshot;
            {
              std::lock_guard<std::mutex> lk(processingMutex);
              auto it=processingJobs.find(id);
              if(it==processingJobs.end()) { sink.write("event: error\ndata: {\"error\":\"Processing job not found\"}\n\n",67); sink.done(); return false; }
              snapshot=*it->second;
            }
            auto msg=sseProgress(snapshot);
            if(!sink.write(msg.data(),msg.size())) return false;
            if(snapshot.status=="completed"||snapshot.status=="failed"||snapshot.status=="cancelled"){sink.done();return false;}
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return true;
          });
    });
    app.Post("/process",[](const httplib::Request& req,httplib::Response& res){
        if(!req.is_multipart_form_data() || !req.has_file("image")) {jsonOut(res,{{"error","No image file was uploaded"}},400);return;}
        const auto& file=req.get_file_value("image");
        if(file.content.empty()||file.content.size()>MAX_UPLOAD){jsonOut(res,{{"error","Invalid or oversized image upload"}},400);return;}
        std::string jobId;
        if(req.has_file("jobId")) jobId=req.get_file_value("jobId").content;
        else if(req.has_param("jobId")) jobId=req.get_param_value("jobId");
        if(!validJobId(jobId)){jsonOut(res,{{"error","Invalid processing job ID"}},400);return;}
        json config=json::object();
        try {
            std::string configText;
            if(req.has_file("config")) configText=req.get_file_value("config").content;
            else if(req.has_param("config")) configText=req.get_param_value("config");
            if(!configText.empty()) config=json::parse(configText);
        } catch(...) {}
        if(!config.is_object()) config=json::object();
        std::vector<std::string> flags;
        for(const char* key:{"water","vegetation","dirt","noise","parallel","shadows","rocks","highlight"})
            flags.push_back(config.value(key,true)?"1":"0");
        cv::Mat decoded=cv::imdecode(std::vector<unsigned char>(file.content.begin(),file.content.end()),cv::IMREAD_COLOR);
        if(decoded.empty()){jsonOut(res,{{"error","The uploaded file is not a supported, valid image."}},400);return;}
        fs::path dir=fs::temp_directory_path()/("sgis-process-"+randomHex(12));
        fs::create_directories(dir);
        fs::path input=dir/"input.jpg", output=dir/"output.jpg";
        if(!cv::imwrite(input.string(),decoded)){fs::remove_all(dir);jsonOut(res,{{"error","Could not prepare uploaded image"}},500);return;}
        auto job=std::make_shared<ProcessingJob>(); job->directory=dir;job->output=output;
        {
          std::lock_guard<std::mutex> lk(processingMutex);
          if(processingJobs.count(jobId)){fs::remove_all(dir);jsonOut(res,{{"error","This processing job ID is already in use"}},409);return;}
          processingJobs[jobId]=job;
        }
        launchProcessor(jobId,input,output,flags);
        jsonOut(res,{{"jobId",jobId},{"status","processing"}},202);
    });
    app.Get(R"(/process-result/([A-Za-z0-9-]{1,100}))",[](const httplib::Request& req,httplib::Response& res){
        const std::string id=req.matches[1]; std::shared_ptr<ProcessingJob> job;
        {std::lock_guard<std::mutex> lk(processingMutex);auto it=processingJobs.find(id);if(it==processingJobs.end()){jsonOut(res,{{"error","Processing job not found or expired"}},404);return;}job=it->second;}
        if(job->status=="processing"){jsonOut(res,{{"status","processing"}},202);return;}
        if(job->status!="completed"){std::lock_guard<std::mutex>lk(processingMutex);processingJobs.erase(id);fs::remove_all(job->directory);jsonOut(res,{{"error",job->error.empty()?"Image processing failed":job->error}},job->status=="cancelled"?410:500);return;}
        res.set_content_provider("image/jpeg", [job](size_t offset,httplib::DataSink& sink){
          std::ifstream f(job->output,std::ios::binary);
          if(!f) return false;
          f.seekg(0,std::ios::end);
          const auto end=f.tellg();
          if(end < 0) return false;
          const size_t fileSize=static_cast<size_t>(end);
          if(offset >= fileSize){ sink.done(); return true; }
          f.seekg(static_cast<std::streamoff>(offset));
          std::vector<char> buffer(64 * 1024);
          f.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
          const auto count=static_cast<size_t>(f.gcount());
          if(count == 0) return false;
          return sink.write(buffer.data(),count);
        });
        res.set_header("Content-Disposition","inline; filename=\"processed.jpg\"");
    });
    app.Delete(R"(/process/([A-Za-z0-9-]{1,100}))",[](const httplib::Request& req,httplib::Response& res){
        const std::string id=req.matches[1];std::shared_ptr<ProcessingJob> job;
        {std::lock_guard<std::mutex>lk(processingMutex);auto it=processingJobs.find(id);if(it==processingJobs.end()){jsonOut(res,{{"error","Processing job not found"}},404);return;}job=it->second;job->status="cancelled";job->error="Processing cancelled";}
        if(job->pid>0) kill(job->pid,SIGTERM);
        fs::remove_all(job->directory);
        jsonOut(res,{{"status","cancelled"}});
    });
    // Area Scan endpoints report capture progress and assemble satellite tiles for a selected map area.
    // Area-image scanning has its own progress state and processing endpoint.
    app.Get(R"(/area-progress/([A-Za-z0-9-]{1,100}))",[](const httplib::Request& req,httplib::Response& res){
        const std::string id=req.matches[1];
        res.set_header("Cache-Control","no-cache, no-transform");res.set_header("X-Accel-Buffering","no");
        res.set_chunked_content_provider("text/event-stream",[id](size_t,httplib::DataSink& sink){
            AreaProgress p;
            {std::lock_guard<std::mutex>lk(areaMutex);auto it=areaJobs.find(id);if(it==areaJobs.end()){const std::string msg="event: error\ndata: {\"error\":\"Area Scan job not found\"}\n\n";sink.write(msg.data(),msg.size());sink.done();return false;}p=it->second;}
            json j={{"percent",p.percent},{"stage",p.stage},{"current",p.current},{"total",p.total}};
            if(p.ready)j["ready"]=true;if(!p.error.empty())j["error"]=p.error;
            std::string msg="data: "+j.dump()+"\n\n";if(!sink.write(msg.data(),msg.size()))return false;
            if(p.ready||!p.error.empty()){sink.done();return false;}
            std::this_thread::sleep_for(std::chrono::milliseconds(500));return true;
        });
    });
    app.Post("/area-image",[](const httplib::Request& req,httplib::Response& res){
      std::string jobId; try {
        json body=json::parse(req.body);
        jobId=body.value("jobId",std::string());
        if(!validJobId(jobId)){jsonOut(res,{{"error","Invalid Area Scan job ID."}},400);return;}
        double n=body.at("north").get<double>(), south=body.at("south").get<double>();
        double e=body.at("east").get<double>(), w=body.at("west").get<double>();
        int z=body.at("zoom").get<int>();
        std::string source=body.value("imagerySource",std::string("aerial"));
        const std::vector<std::string> sources={"aerial","otago-2025-0.2m","canterbury-2024-0.2m","bay-of-plenty-2024-0.2m","wellington-2025-0.2m","west-coast-2024-2026-0.2m","hawkes-bay-2025-2026-0.2m"};
        if(!(std::isfinite(n)&&std::isfinite(south)&&std::isfinite(e)&&std::isfinite(w))||n<=south||e<=w||n>85.0511||south< -85.0511||e>180||w< -180||(z!=18&&z!=19&&z!=20)||std::find(sources.begin(),sources.end(),source)==sources.end()){
          jsonOut(res,{{"error","Invalid area bounds or zoom level."}},400);return;
        }
        constexpr double eps=1e-9;
        int minX=(int)std::floor(tileXPos(w,z)+eps), maxX=(int)std::ceil(tileXPos(e,z)-eps)-1;
        int minY=(int)std::floor(tileYPos(n,z)+eps), maxY=(int)std::ceil(tileYPos(south,z)-eps)-1;
        int cols=maxX-minX+1, rows=maxY-minY+1, total=cols*rows;
        if(cols<=0||rows<=0||total>2048||cols*256>16384||rows*256>8192){
          jsonOut(res,{{"error","Area requires "+std::to_string(total)+" tiles ("+std::to_string(cols)+" × "+std::to_string(rows)+"). Maximum 2048 tiles and 16384 × 8192 pixels. Try moving the selection box slightly and capturing again. This may bring the area within the limit."}},413);return;
        }
        {std::lock_guard<std::mutex>lk(areaMutex);areaJobs[jobId]=AreaProgress{0,0,total,"Starting tile downloads","",false};}
        cv::Mat canvas(rows*256,cols*256,CV_8UC3,cv::Scalar(255,255,255));
        int done=0;
        const char* api=std::getenv("LINZ_API_KEY");
        std::string apiKey=api?api:"c01ksp9a9c1aweex6jqa4dr3raw";
      std::vector<std::pair<int,int>> tileJobs;
      for(int by=minY;by<=maxY;++by){
        for(int bx=minX;bx<=maxX;++bx){
          tileJobs.emplace_back(bx,by);
        }
      }

      constexpr size_t BATCH_SIZE=8;

      for(size_t batchStart=0;batchStart<tileJobs.size();batchStart+=BATCH_SIZE){
        size_t batchEnd=std::min(batchStart+BATCH_SIZE,tileJobs.size());

        std::vector<std::future<std::vector<unsigned char>>> downloads;
        downloads.reserve(batchEnd-batchStart);

        // Download up to eight LINZ tiles concurrently.
        for(size_t i=batchStart;i<batchEnd;++i){
          int bx=tileJobs[i].first;
          int by=tileJobs[i].second;

          std::string url="https://basemaps.linz.govt.nz/v1/tiles/"+source+"/WebMercatorQuad/"+std::to_string(z)+"/"+std::to_string(bx)+"/"+std::to_string(by)+".webp?api="+apiKey;

          downloads.emplace_back(std::async(std::launch::async,[url](){
            return downloadTile(url);
          }));
        }

        // Retrieve each tile and stitch it into its original position.
        for(size_t i=batchStart;i<batchEnd;++i){
          int bx=tileJobs[i].first;
          int by=tileJobs[i].second;

          auto bytes=downloads[i-batchStart].get();
          cv::Mat tile=cv::imdecode(bytes,cv::IMREAD_COLOR);

          if(tile.empty()) throw std::runtime_error("Could not decode LINZ tile "+std::to_string(z)+"/"+std::to_string(bx)+"/"+std::to_string(by));

          cv::resize(tile,tile,cv::Size(256,256));
          tile.copyTo(canvas(cv::Rect((bx-minX)*256,(by-minY)*256,256,256)));

          ++done;
          updateArea(jobId,AreaProgress{(int)std::round(done*85.0/total),done,total,"Downloading satellite tiles","",false});
        }
      }

              updateArea(jobId,AreaProgress{90,total,total,"Stitching satellite imagery","",false});
        std::vector<unsigned char> encoded;
        if(!cv::imencode(".jpg",canvas,encoded,{cv::IMWRITE_JPEG_QUALITY,95}))throw std::runtime_error("Could not encode stitched image");
        updateArea(jobId,AreaProgress{100,total,total,"Area capture complete","",true});
        res.set_content(reinterpret_cast<const char*>(encoded.data()),encoded.size(),"image/jpeg");
      } catch(const std::exception& ex) {
        if(!jobId.empty()) updateArea(jobId,AreaProgress{0,0,0,"Area Scan failed",ex.what(),false});
        jsonOut(res,{{"error","LINZ imagery download or stitching failed."},{"details",ex.what()}},502);
      }
    });

    // Start listening on all container interfaces so the frontend proxy can reach the API.
    std::cout<<"SGIS C++ server starting on 0.0.0.0:"<<PORT<<"\nDatabase: "<<dbPath()<<"\n";
    app.listen("0.0.0.0",PORT);
 }catch(const std::exception& e){std::cerr<<"Fatal: "<<e.what()<<"\n";return 1;}
 return 0;
}
