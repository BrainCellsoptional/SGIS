#include "Algorithum_set.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <mutex>

Algorithum::Algorithum() {}

void Algorithum::setProgressCallback(
    std::function<void(int, const std::string&, int, int)> callback
)
{
    progressCallback = std::move(callback);
}

void Algorithum::reportProgress(
    const std::string& stage,
    double stageFraction,
    int current,
    int total
)
{
    if (!progressCallback || progressTotalStages <= 0)
        return;

    const double completed =
        progressCompletedStages + std::clamp(stageFraction, 0.0, 1.0);

    // Reserve the final 5% for successfully saving the output image.
    const int percent = static_cast<int>(
        completed * 95.0 / progressTotalStages
    );

    progressCallback(percent, stage, current, total);
}

void Algorithum::completeProgressStage(const std::string& stage)
{
    reportProgress(stage, 1.0);
    ++progressCompletedStages;
}

// Loads the source satellite image and initialises a mask of pixels still eligible for detection.
bool Algorithum::loadImage(const std::string &path)
{
    image = cv::imread(path);

    if (image.empty())
        return false;

    mask = cv::Mat(image.size(), CV_8UC1, cv::Scalar(255));

    return true;
}

// Writes the processed image to the requested output path.
bool Algorithum::saveImage(const std::string &path)
{

    return cv::imwrite(path, image);
}

// Darkens pixels excluded by the mask so the remaining detected regions stand out.
void Algorithum::highlightMask()
{
    const int chunkSize = 256;

    for (int startRow = 0; startRow < image.rows; startRow += chunkSize)
    {
        const int endRow = std::min(startRow + chunkSize, image.rows);

        cv::parallel_for_(cv::Range(startRow, endRow),
            [&](const cv::Range &range)
            {
                for (int y = range.start; y < range.end; y++)
                {
                    for (int x = 0; x < image.cols; x++)
                    {
                        if (mask.at<uchar>(y, x) == 0)
                        {
                            cv::Vec3b &pixel = image.at<cv::Vec3b>(y, x);

                            pixel[0] = cv::saturate_cast<uchar>(pixel[0] * 0.4);
                            pixel[1] = cv::saturate_cast<uchar>(pixel[1] * 0.4);
                            pixel[2] = cv::saturate_cast<uchar>(pixel[2] * 0.4);

                            continue;
                        }
                    }
                }
            });

        reportProgress(
            "Highlighting image",
            static_cast<double>(endRow) / image.rows,
            endRow,
            image.rows
        );
    }
}

// Applies a Gaussian blur to reduce small image variations before filtering.
void Algorithum::blurImage(cv::Mat &src)
{
    cv::GaussianBlur(
        src,
        src,
        cv::Size(5, 5),
        0);
}

// Identifies water-coloured pixels in HSV space and excludes them from the valid-pixel mask.
void Algorithum::removeWater(cv::Mat &src)
{
    reportProgress("Removing water - converting image", 0.1);
    cv::Mat hsv;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
    reportProgress("Removing water - identifying regions", 0.4);

    cv::Mat waterMask;

    cv::inRange(
        hsv,
        cv::Scalar(90, 50, 20),
        cv::Scalar(140, 255, 200),
        waterMask);

    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(5, 5));

    // Removes some noise
    cv::morphologyEx(
        waterMask,
        waterMask,
        cv::MORPH_CLOSE,
        kernel);

    reportProgress("Removing water - applying mask", 0.9);
    mask.setTo(0, waterMask);
    reportProgress("Removing water - complete", 1.0);
}

// Identifies vegetation using an HSV colour range and excludes those pixels from the mask.
void Algorithum::removeVegetation(cv::Mat &src)
{
    reportProgress("Removing vegetation - converting image", 0.1);
    cv::Mat hsv;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
    reportProgress("Removing vegetation - identifying regions", 0.4);

    cv::Mat vegetationMask;

    cv::inRange(hsv,
                cv::Scalar(35, 40, 40),
                cv::Scalar(85, 255, 255),
                vegetationMask);

    // Creates a Tiny Buffer, adjust later
    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(1, 1)
    );

    cv::dilate(vegetationMask, vegetationMask, kernel);


    reportProgress("Removing vegetation - applying mask", 0.9);
    mask.setTo(0, vegetationMask);
    reportProgress("Removing vegetation - complete", 1.0);
}

// Combines soil-colour and local texture information to identify likely dirt regions.
void Algorithum::removeDirt(cv::Mat &src)
{
    cv::Mat hsv;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);

    cv::Mat soilMask;
    cv::inRange(
        hsv,
        cv::Scalar(0, 0, 30),
        cv::Scalar(75, 180, 255),
        soilMask
    );

    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);

    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_16S);

    cv::Mat texture;
    cv::convertScaleAbs(lap, texture);

    const int chunkSize = 256;

    for (int startRow = 0; startRow < src.rows; startRow += chunkSize)
    {
        const int endRow = std::min(startRow + chunkSize, src.rows);

        cv::parallel_for_(cv::Range(startRow, endRow),
            [&](const cv::Range &range)
            {
                for (int y = range.start; y < range.end; y++)
                {
                    for (int x = 0; x < src.cols; x++)
                    {
                        uchar soil = soilMask.at<uchar>(y, x);
                        uchar t = texture.at<uchar>(y, x);

                        if (soil && t < 8)
                        {
                            mask.at<uchar>(y, x) = 0;
                        }
                    }
                }
            });

        reportProgress(
            "Removing dirt",
            static_cast<double>(endRow) / src.rows,
            endRow,
            src.rows
        );
    }
}

// Detects dark, low-contrast regions and removes them as likely shadows.
void Algorithum::detectShadows(cv::Mat &src, cv::Mat &shadowMask)
{
    shadowMask = cv::Mat::zeros(src.size(), CV_8UC1);

    cv::Mat hsv, gray;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);

    cv::Mat blur;
    cv::GaussianBlur(gray, blur, cv::Size(7, 7), 0);

    cv::Mat contrast;
    cv::absdiff(gray, blur, contrast);

    const int chunkSize = 256;

    for (int startRow = 0; startRow < src.rows; startRow += chunkSize)
    {
        const int endRow = std::min(startRow + chunkSize, src.rows);

        cv::parallel_for_(cv::Range(startRow, endRow),
            [&](const cv::Range &range)
            {
                for (int y = range.start; y < range.end; y++)
                {
                    for (int x = 0; x < src.cols; x++)
                    {
                        uchar V = hsv.at<cv::Vec3b>(y, x)[2];
                        uchar C = contrast.at<uchar>(y, x);
                        uchar T = gray.at<uchar>(y, x);

                        bool isRockLike = (C > 12);

                        if (V < 120 && C < 20 && !isRockLike)
                        {
                            shadowMask.at<uchar>(y, x) = 255;
                        }
                    }
                }
            });

        reportProgress(
            "Detecting shadows",
            static_cast<double>(endRow) / src.rows,
            endRow,
            src.rows
        );
    }

    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(3, 3)
    );

    cv::morphologyEx(shadowMask, shadowMask, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(shadowMask, shadowMask, cv::MORPH_CLOSE, kernel);

    mask.setTo(0, shadowMask);
}

// Removes small connected regions from the remaining mask to reduce isolated detections.
void Algorithum::removeNoise(cv::Mat &src)
{
    cv::Mat labels, stats, centroids;
    cv::Mat hsv;
    cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV);

    int count = cv::connectedComponentsWithStats(
        mask, labels, stats, centroids, 8, CV_32S
    );

    cv::Mat cleaned = cv::Mat::zeros(mask.size(), CV_8UC1);

    std::vector<uchar> keepComponent(count, 0);

    // First pass: filter connected components.
    for (int i = 1; i < count; i++)
    {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);

        if (area >= 30)
            keepComponent[i] = 1;

        if ((i % 64 == 0) || i == count - 1)
        {
            reportProgress(
                "Filtering noise components",
                0.45 * static_cast<double>(i) / std::max(1, count - 1),
                i,
                count - 1
            );
        }
    }

    const int chunkSize = 256;

    for (int startRow = 0; startRow < labels.rows; startRow += chunkSize)
    {
        const int endRow = std::min(startRow + chunkSize, labels.rows);

        cv::parallel_for_(cv::Range(startRow, endRow),
            [&](const cv::Range &range)
            {
                for (int y = range.start; y < range.end; y++)
                {
                    const int *labelRow = labels.ptr<int>(y);
                    uchar *cleanRow = cleaned.ptr<uchar>(y);

                    for (int x = 0; x < labels.cols; x++)
                    {
                        int component = labelRow[x];

                        if (component > 0 && keepComponent[component])
                            cleanRow[x] = 255;
                    }
                }
            });

        reportProgress(
            "Rebuilding noise mask",
            0.45 + 0.55 * static_cast<double>(endRow) / labels.rows,
            endRow,
            labels.rows
        );
    }

    mask = cleaned;
}

// Detects pairs of nearby parallel edges and removes the bounded regions between them.
void Algorithum::removeParallelStructures(cv::Mat &src)
{
    // Conservative road/building-edge detector.
    // Unlike the previous implementation, it only removes the narrow,
    // bounded corridor between nearby, similarly oriented line segments.
    cv::Mat gray, edges;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 1.2);

    reportProgress("Detecting edges", 0.05);
    cv::Canny(gray, edges, 60, 170);
    reportProgress("Detecting edges", 0.20);

    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180.0, 85, 40, 12);
    reportProgress("Detecting straight lines", 0.30);

    // Limit the comparison set to the strongest (longest) line segments.
    // This bounds the pair search on detailed satellite imagery.
    std::sort(lines.begin(), lines.end(), [](const cv::Vec4i& a, const cv::Vec4i& b) {
        const double al = std::hypot(double(a[2] - a[0]), double(a[3] - a[1]));
        const double bl = std::hypot(double(b[2] - b[0]), double(b[3] - b[1]));
        return al > bl;
    });
    constexpr size_t MAX_LINES = 1200;
    if (lines.size() > MAX_LINES)
        lines.resize(MAX_LINES);

    cv::Mat structureMask = cv::Mat::zeros(src.size(), CV_8UC1);
    const double imageDiagonal = std::hypot(double(src.cols), double(src.rows));
    const double maxSpacing = std::clamp(imageDiagonal * 0.025, 18.0, 120.0);
    constexpr double MIN_SPACING = 5.0;
    constexpr double ANGLE_TOLERANCE = 0.10; // about 5.7 degrees
    constexpr double MIN_OVERLAP = 0.60;

    struct Segment {
        cv::Point2d p1, p2, direction;
        double angle = 0.0;
        double length = 0.0;
    };
    std::vector<Segment> segments;
    segments.reserve(lines.size());
    for (const auto& l : lines) {
        cv::Point2d a(l[0], l[1]), b(l[2], l[3]);
        cv::Point2d d = b - a;
        double len = cv::norm(d);
        if (len < 40.0) continue;
        d *= (1.0 / len);
        double angle = std::atan2(d.y, d.x);
        // Treat opposite directions as the same line orientation.
        if (angle < 0.0) angle += CV_PI;
        if (angle >= CV_PI) angle -= CV_PI;
        segments.push_back({a, b, d, angle, len});
    }

    // Each candidate pair must be close, parallel, and overlap along its
    // length. The filled region is therefore bounded by two actual edges.
    const size_t n = segments.size();
    const size_t totalPairs = n * (n > 0 ? n - 1 : 0) / 2;
    size_t checkedPairs = 0;
    size_t acceptedPairs = 0;

    for (size_t i = 0; i < n; ++i) {
        const Segment& a = segments[i];
        cv::Point2d u = a.direction;
        cv::Point2d normal(-u.y, u.x);
        double a0 = a.p1.dot(u), a1 = a.p2.dot(u);
        if (a0 > a1) std::swap(a0, a1);
        double an = ((a.p1.dot(normal)) + (a.p2.dot(normal))) * 0.5;

        for (size_t j = i + 1; j < n; ++j) {
            const Segment& b = segments[j];
            ++checkedPairs;

            double angleDiff = std::fabs(a.angle - b.angle);
            angleDiff = std::min(angleDiff, CV_PI - angleDiff);
            if (angleDiff > ANGLE_TOLERANCE) continue;
            if (std::min(a.length, b.length) / std::max(a.length, b.length) < 0.50) continue;

            double b0 = b.p1.dot(u), b1 = b.p2.dot(u);
            if (b0 > b1) std::swap(b0, b1);
            double overlapStart = std::max(a0, b0);
            double overlapEnd = std::min(a1, b1);
            double overlap = overlapEnd - overlapStart;
            double shorterLength = std::min(a.length, b.length);
            if (overlap < shorterLength * MIN_OVERLAP || overlap < 35.0) continue;

            double bn = ((b.p1.dot(normal)) + (b.p2.dot(normal))) * 0.5;
            double spacing = std::fabs(bn - an);
            if (spacing < MIN_SPACING || spacing > maxSpacing) continue;

            // Reject very short, extremely wide, or implausibly large regions.
            if (overlap / spacing < 1.2) continue;
            if (overlap * spacing > double(src.rows) * src.cols * 0.08) continue;

            auto pointAt = [&](double t, double offset) {
                cv::Point2d p = u * t + normal * offset;
                return cv::Point(cvRound(p.x), cvRound(p.y));
            };
            const double margin = 2.0;
            std::vector<cv::Point> corridor = {
                pointAt(overlapStart - margin, an),
                pointAt(overlapEnd + margin, an),
                pointAt(overlapEnd + margin, bn),
                pointAt(overlapStart - margin, bn)
            };
            cv::fillConvexPoly(structureMask, corridor, cv::Scalar(255));
            ++acceptedPairs;
        }

        if ((i % 16 == 0) || i + 1 == n) {
            const double fraction = totalPairs > 0
                ? static_cast<double>(checkedPairs) / static_cast<double>(totalPairs)
                : 1.0;

            reportProgress(
                "Checking road and building edges",
                0.30 + 0.60 * fraction,
                static_cast<int>(checkedPairs),
                static_cast<int>(totalPairs)
            );
        }
    }

    if (totalPairs == 0) {
        reportProgress("Checking road and building edges", 0.90, 1, 1);
    }

    (void)acceptedPairs;
    reportProgress("Cleaning structure mask", 0.95);

    // A very small expansion joins edge pixels without creating the broad
    // removal zones produced by the previous 5x5 dilation.
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::dilate(structureMask, structureMask, kernel);
    mask.setTo(0, structureMask);
}

// Group rock like pixel and draw a circle
// Finds connected candidate regions and highlights the detected rock-like groups.
void Algorithum::detectRockGroups(cv::Mat &output)
{
    reportProgress(
        "Draw Rocks - Check 1: Finding candidate regions",
        0.0
    );

    cv::Mat labels, stats, centroids;

    int count = cv::connectedComponentsWithStats(
        mask, labels, stats, centroids, 8, CV_32S
    );

    struct CircleResult
    {
        cv::Point2f center;
        float radius = 0.0f;
        bool valid = false;
    };

    std::vector<CircleResult> results(count);

    // Progress instrumentation only. Detection calculations are unchanged.
    const int componentTotal = count - 1;
    const int reportInterval = std::max(1, componentTotal / 100);
    int analysedComponents = 0;
    std::mutex progressMutex;

    auto recordComponentProgress = [&]()
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        ++analysedComponents;

        if (analysedComponents % reportInterval == 0 ||
            analysedComponents == componentTotal)
        {
            reportProgress(
                "Draw Rocks - Check 2: Analysing candidate groups",
                0.8 * static_cast<double>(analysedComponents) /
                    std::max(1, componentTotal),
                analysedComponents,
                componentTotal
            );
        }
    };

    reportProgress(
        "Draw Rocks - Check 2: Analysing candidate groups",
        0.0, 0, componentTotal
    );

    // Each component is processed independently.
    cv::parallel_for_(cv::Range(1, count),
        [&](const cv::Range &range)
        {
            for (int i = range.start; i < range.end; i++)
            {
                int area = stats.at<int>(i, cv::CC_STAT_AREA);

                if (area < 40)
                {
                    recordComponentProgress();
                    continue;
                }

                std::vector<cv::Point> points;

                // Keep the original full-image scan and pixel order.
                for (int y = 0; y < labels.rows; y++)
                {
                    for (int x = 0; x < labels.cols; x++)
                    {
                        if (labels.at<int>(y, x) == i)
                            points.push_back(cv::Point(x, y));
                    }
                }

                if (points.empty())
                {
                    recordComponentProgress();
                    continue;
                }

                cv::minEnclosingCircle(
                    points,
                    results[i].center,
                    results[i].radius
                );

                results[i].valid = true;
                recordComponentProgress();
            }
        });

    reportProgress(
        "Draw Rocks - Check 3: Drawing detected rock outlines",
        0.8, 0, componentTotal
    );

    // Draw in original component order.
    for (int i = 1; i < count; i++)
    {
        if (results[i].valid)
        {
            cv::circle(
                output,
                results[i].center,
                results[i].radius,
                cv::Scalar(0, 0, 255),
                2
            );
        }

        if ((i % 64 == 0) || i == count - 1)
        {
            reportProgress(
                "Draw Rocks - Check 3: Drawing detected rock outlines",
                0.8 + 0.2 * static_cast<double>(i) /
                    std::max(1, count - 1),
                i,
                count - 1
            );
        }
    }
}

// Function Function
// Runs the enabled processing stages in sequence and reports progress to the caller.
void Algorithum::processSatelliteImage(const ProcessConfig& config)
{
    progressCompletedStages = 0;

    // Count only stages that will actually execute.
    progressTotalStages =
        1 +
        static_cast<int>(config.water) +
        static_cast<int>(config.vegetation) +
        static_cast<int>(config.dirt) +
        static_cast<int>(config.noise) +
        static_cast<int>(config.parallel) +
        static_cast<int>(config.shadows) +
        static_cast<int>(config.rocks) +
        static_cast<int>(config.highlight);

    cv::Mat shadowMask;

    cv::Mat original = image.clone();
    cv::Mat terrainRemoved = original.clone();

    reportProgress("Blurring image", 0.0);
    cv::GaussianBlur(terrainRemoved, terrainRemoved, cv::Size(5, 5), 0);
    completeProgressStage("Blurring image");

    if (config.water)
    {
        reportProgress("Removing water", 0.0);
        removeWater(terrainRemoved);
        completeProgressStage("Removing water");
    }

    if (config.vegetation)
    {
        reportProgress("Removing vegetation", 0.0);
        removeVegetation(terrainRemoved);
        completeProgressStage("Removing vegetation");
    }

    if (config.dirt)
    {
        reportProgress("Removing dirt", 0.0);
        removeDirt(terrainRemoved);
        completeProgressStage("Removing dirt");
    }

    cv::Mat postProcessed = terrainRemoved.clone();

    if (config.noise)
    {
        reportProgress("Removing noise", 0.0);
        removeNoise(postProcessed);
        completeProgressStage("Removing noise");
    }

    if (config.parallel)
    {
        reportProgress("Removing parallel structures", 0.0);
        removeParallelStructures(postProcessed);
        completeProgressStage("Removing parallel structures");
    }

    if (config.shadows)
    {
        reportProgress("Detecting shadows", 0.0);
        detectShadows(postProcessed, shadowMask);
        completeProgressStage("Detecting shadows");
    }

    cv::Mat finalOutput = postProcessed.clone();

    if (config.rocks)
    {
        reportProgress("Detecting rock groups", 0.0);
        detectRockGroups(finalOutput);
        completeProgressStage("Detecting rock groups");
    }

    image = finalOutput;

    if (config.highlight)
    {
        reportProgress("Highlighting image", 0.0);
        highlightMask();
        completeProgressStage("Highlighting image");
    }
}