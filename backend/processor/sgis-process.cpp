#include "Algorithum_set.h"

#include <iostream>
#include <string>

// Command-line entry point launched by the backend for each image-processing job.
int main(int argc, char* argv[])
{
    // Arguments:
    // input output water vegetation dirt noise
    // parallel shadows rocks highlight

    if (argc != 11)
    {
        std::cerr << "Usage: sgis-process input output "
                  << "water vegetation dirt noise "
                  << "parallel shadows rocks highlight\n";
        return 1;
    }

    try
    {
        // Allow OpenCV to use all 16 physical CPU cores.
        cv::setNumThreads(16);

        ProcessConfig config;

        config.water      = std::stoi(argv[3]) != 0;
        config.vegetation = std::stoi(argv[4]) != 0;
        config.dirt       = std::stoi(argv[5]) != 0;
        config.noise      = std::stoi(argv[6]) != 0;
        config.parallel   = std::stoi(argv[7]) != 0;
        config.shadows    = std::stoi(argv[8]) != 0;
        config.rocks      = std::stoi(argv[9]) != 0;
        config.highlight  = std::stoi(argv[10]) != 0;

        Algorithum processor;

        // Progress messages use a pipe-delimited format read by the backend process manager.
        processor.setProgressCallback(
            [](int percent, const std::string& stage, int current, int total)
            {
                std::cout << "SGIS_PROGRESS|"
                          << percent << "|"
                          << stage << "|"
                          << current << "|"
                          << total << std::endl;
            }
        );

        if (!processor.loadImage(argv[1]))
        {
            std::cerr << "Failed to load input image\n";
            return 2;
        }

        // Apply the selected filters before writing the result image.
        processor.processSatelliteImage(config);

        std::cout << "SGIS_PROGRESS|95|Saving processed image|0|1" << std::endl;

        if (!processor.saveImage(argv[2]))
        {
            std::cerr << "Failed to save processed image\n";
            return 3;
        }

        std::cout << "SGIS_PROGRESS|100|Image saved successfully|1|1" << std::endl;
        std::cout << "Image processing successful\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Processing error: " << error.what() << '\n';
        return 4;
    }
}
