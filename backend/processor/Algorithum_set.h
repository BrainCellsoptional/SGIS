#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <functional>

// Stores which image processing filters are enabled.
// Each value controls whether a specific stage runs in the pipeline.
struct ProcessConfig
{
    bool water = true;
    bool vegetation = true;
    bool dirt = true;
    bool noise = true;
    bool parallel = true;
    bool shadows = true;
    bool rocks = true;
    bool highlight = true;
};

// Main image processing class for the SGIS system.
// Provides functions for loading, filtering, analysing,
// highlighting, and saving satellite images.
class Algorithum
{
public:
    Algorithum();

    bool loadImage(const std::string &path);
    bool saveImage(const std::string &path);

    void highlightMask();
    void blurImage(cv::Mat &src);

    void removeWater(cv::Mat &src);
    void removeVegetation(cv::Mat &src);
    void removeDirt(cv::Mat &src);

    void removeNoise(cv::Mat &src);
    void removeParallelStructures(cv::Mat &src);

    void detectShadows(cv::Mat &src, cv::Mat &shadowMask);

    void detectRockGroups(cv::Mat &src);

    void processSatelliteImage(const ProcessConfig& config);
    void setProgressCallback(
        std::function<void(int, const std::string&, int, int)> callback
    );

    void reportProgress(
        const std::string& stage,
        double stageFraction,
        int current = 0,
        int total = 0
    );

    void completeProgressStage(const std::string& stage);


private:
    std::function<void(int, const std::string&, int, int)> progressCallback;
    int progressCompletedStages = 0;
    int progressTotalStages = 1;
    cv::Mat image; // Source image, updated as the processing pipeline runs.
    cv::Mat mask;  // Single-channel mask: 255 = keep candidate pixel, 0 = exclude pixel.
};