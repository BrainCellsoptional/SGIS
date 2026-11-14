# SGIS C++ Backend

The SGIS backend is written in C++ using cpp-httplib.
It provides the REST API used by the Vue.js frontend.

## Main Components

- src/main.cpp: HTTP API, authentication, database access,
  image uploads and processing job management.
- processor/Algorithum.cpp: OpenCV image-processing implementation.
- processor/Algorithum_set.h: Image-processing configuration
  and algorithm declarations.
- processor/sgis-process.cpp: Image-processing executable entry point.
- CMakeLists.txt: Backend build configuration.
- Containerfile: Instructions for building the backend container.

## Technologies

- C++
- cpp-httplib
- SQLite
- OpenCV
- OpenSSL
- libcurl
- nlohmann/json

## Building

Build from the SGIS project root:

    podman build -f backend/Containerfile -t localhost/sgis-cpp:latest .

## Running with the Full Application

From the SGIS project root:

    podman compose -f deployment/compose.yaml up -d --build

The backend listens on port 3000 inside the container.

The application database and image directory are mounted into
the backend container so that application data persists.

## Environment Variables

- SGIS_DATA_DIR: Location of application data inside the container.
- SGIS_PROCESSOR: Location of the image-processing executable.
- LINZ_API_KEY: Optional LINZ API key override.

The image-processing executable is launched by the backend
when an image-processing job is submitted.


## Setting Up the Database and Image Directories

The GitHub repository does not include the SQLite database or satellite image dataset. These are excluded to keep the repository size manageable and prevent personal data from being published.

Before running SGIS, create the following directories in the project root:

```bash
mkdir -p database images
```

### Database

The application requires an SQLite database named `sgis.db` inside the `database` directory.

The database is not included in this repository. A fresh database must be initialised using the application's database schema. Existing user accounts and application data are not supplied.

### Images

The `images` directory stores uploaded satellite imagery and processed images.

This directory starts empty. Images can be added through the application's upload functionality once the application is running and a user account has been configured.

### Important

The application source code and deployment configuration are included, but the original database and image dataset must be supplied or generated separately.
