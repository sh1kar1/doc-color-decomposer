#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <regex>

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgcodecs/imgcodecs.hpp>

#include "doc_color_decomposer/doc_color_decomposer.h"

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);

  if (args.size() >= 2) {
    std::filesystem::path src_path = args[0];
    std::filesystem::path dst_path = args[1];

    std::filesystem::path groundtruth = "";
    int tolerance = 35;

    bool nopreprocess = false;
    bool masking = false;
    bool visualize = false;

    for (const auto& arg : args | std::views::drop(2)) {
      if (std::regex_match(arg, std::regex("^--groundtruth=.+$"))) {
        groundtruth = arg.substr(std::string("--groundtruth=").size());
      } else if (std::regex_match(arg, std::regex("^--tolerance=[0-9]*[13579]$"))) {
        tolerance = std::stoi(arg.substr(std::string("--tolerance=").size()));

      } else if (arg == "--nopreprocess") {
        nopreprocess = true;
      } else if (arg == "--masking") {
        masking = true;
      } else if (arg == "--visualize") {
        visualize = true;

      } else {
        std::cerr << "Error: invalid arguments\n";
        std::cerr << "Checkout `./doc-color-decomposer --help`";
        return 1;
      }
    }

    try {
      if (!std::filesystem::exists(dst_path) || !std::filesystem::is_directory(dst_path)) {
        std::filesystem::create_directory(dst_path);
      }

    } catch (...) {
      std::cerr << "Error: invalid arguments\n";
      std::cerr << "Checkout `./doc-color-decomposer --help`";
      return 1;
    }

    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_FATAL);

    doc_color_decomposer::DocColorDecomposer dcd;
    try {
      cv::Mat src = cv::imread(src_path.string(), cv::IMREAD_COLOR);
      dcd = doc_color_decomposer::DocColorDecomposer(src, tolerance, !nopreprocess);

    } catch (...) {
      std::cerr << "Error: invalid image";
      return 1;
    }

    try {
      if (!std::filesystem::exists(dst_path) || !std::filesystem::is_directory(dst_path)) {
        std::filesystem::create_directory(dst_path);
      }

    } catch (...) {
      std::cerr << "Error: invalid arguments\n";
      std::cerr << "Checkout `./doc-color-decomposer --help`";
      return 1;
    }

    try {
      if (!groundtruth.empty()) {
        std::vector<cv::Mat> truth_masks;
        for (const auto& truth_mask_file : std::filesystem::directory_iterator(groundtruth)) {
          truth_masks.push_back(cv::imread(truth_mask_file.path().string(), cv::IMREAD_GRAYSCALE));
        }

        std::ofstream(dst_path / (src_path.stem().string() + "-quality.txt")) << dcd.ComputeQuality(truth_masks);
      }

    } catch (...) {
      std::cerr << "Error: invalid masks";
      return 1;
    }

    for (const auto& [layer_idx, layer] : (masking ? dcd.GetMasks() : dcd.GetLayers()) | std::views::enumerate) {
      cv::imwrite((dst_path / (src_path.stem().string() + "-layer-")).string() + std::to_string(layer_idx + 1) + ".png", layer);
    }

    if (visualize) {
      cv::imwrite((dst_path / (src_path.stem().string() + "-plot-2d-lab.png")).string(), dcd.Plot2DLab());

      std::ofstream(dst_path / (src_path.stem().string() + "-plot-3d-rgb.tex")) << dcd.Plot3DRgb();
      std::ofstream(dst_path / (src_path.stem().string() + "-plot-1d-phi.tex")) << dcd.Plot1DPhi();
      std::ofstream(dst_path / (src_path.stem().string() + "-plot-1d-clusters.tex")) << dcd.Plot1DClusters();
    }

    std::cout << "Success: files saved";

  } else if (args.empty() || args.size() == 1 && args[0] == "--help") {
    std::cout << "DESCRIPTION\n";
    std::cout << "  App of the `Doc Color Decomposer` library for documents decomposition by color clustering\n";
    std::cout << "  More info: https://github.com/Sh1kar1/doc-color-decomposer\n\n";

    std::cout << "SYNOPSIS\n";
    std::cout << "  ./doc-color-decomposer <path-to-image> <path-to-output-directory> [options]\n\n";

    std::cout << "OPTIONS\n";
    std::cout << "  --groundtruth=<path-to-directory-with-masks>  Set path to truth image masks and compute quality\n";
    std::cout << "  --tolerance=<odd-positive-value>              Set tolerance of decomposition (default: 35)\n";
    std::cout << "  --nopreprocess                                Disable image preprocessing by aberration reduction\n";
    std::cout << "  --masking                                     Save binary masks instead of layers\n";
    std::cout << "  --visualize                                   Save visualizations";

  } else {
    std::cerr << "Error: invalid arguments\n";
    std::cerr << "Checkout `./doc-color-decomposer --help`";
    return 1;
  }

  return 0;
}
