# SGIS – Simple Geographic Information System

SGIS is a web-based geographic information system designed to assist geological fieldwork by processing satellite imagery and highlighting potential rock outcroppings.

## Project Structure

- `backend/` – C++ API server and image-processing application.
- `frontend/` – Vue.js web application.
- `database/` – Database schema for a fresh SQLite database.
- `deployment/` – Podman Compose and Nginx configuration.
- `images/` – Uploaded and processed satellite imagery.

## Requirements

- Linux environment
- Podman
- podman-compose
- Git

## Installation

### 1. Clone the repository

    git clone <repository-url>
    cd SGIS

### 2. Create storage directories

    mkdir -p database images

The database and image files are not included in the repository. The backend automatically creates and initialises the SQLite database when it starts.

### 3. Build and start SGIS

    podman compose -f deployment/compose.yaml up -d --build

This builds the C++ backend and Vue frontend, then starts both services.

### 4. Access the application

Frontend: http://localhost:8080

Backend API: http://localhost:3000

### 5. User accounts

User accounts are created manually by the system administrator. A fresh installation does not include existing user accounts.

## Database and Image Storage

The SQLite database and uploaded satellite imagery are stored locally and excluded from version control.

- Database location: `database/`
- Image storage location: `images/`

These directories are mounted into the backend container so data persists when containers are restarted or rebuilt.

## Stopping SGIS

    podman compose -f deployment/compose.yaml down

## Rebuilding after code changes

    podman compose -f deployment/compose.yaml up -d --build

## Notes

- Satellite imagery access requires a valid LINZ Basemaps API key.
- Existing user accounts, database records, and uploaded imagery are not included in the repository.
- The application automatically initialises a fresh database.
