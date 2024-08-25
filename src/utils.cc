#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <ranges>

#include <opencv2/imgproc/imgproc.hpp>

namespace doc_color_decomposer {

cv::Mat ThreshS(cv::Mat src, double thresh) {
  cv::cvtColor(src, src, cv::COLOR_BGR2HSV_FULL);

  std::vector<cv::Mat> hsv_channels;
  cv::split(src, hsv_channels);

  cv::threshold(hsv_channels[1], hsv_channels[1], thresh, 0.0, cv::THRESH_TOZERO);

  cv::merge(hsv_channels, src);

  cv::cvtColor(src, src, cv::COLOR_HSV2BGR_FULL);

  return src;
}

cv::Mat ThreshL(cv::Mat src, double thresh) {
  cv::cvtColor(src, src, cv::COLOR_BGR2HLS_FULL);

  std::vector<cv::Mat> hls_channels;
  cv::split(src, hls_channels);

  cv::threshold(hls_channels[1], hls_channels[1], thresh, 0.0, cv::THRESH_TOZERO);

  cv::merge(hls_channels, src);

  cv::cvtColor(src, src, cv::COLOR_HLS2BGR_FULL);

  return src;
}

std::map<std::array<int, 3>, int> ComputeColorToN(const cv::Mat& src) {
  std::map<std::array<int, 3>, int> rgb_to_n;

  for (const auto& [y, x] : std::views::cartesian_product(std::views::iota(0, src.rows), std::views::iota(0, src.cols))) {
    const auto& pixel = src.at<cv::Vec3b>(y, x);
    std::array<int, 3> rgb = {pixel[2], pixel[1], pixel[0]};

    ++rgb_to_n[rgb];
  }

  return rgb_to_n;
}

cv::Mat ProjOnPlane(const cv::Mat& point, const cv::Mat& center, const cv::Mat& norm, const cv::Mat& transformation) {
  cv::Mat default_proj = (cv::Mat_<int>(1, 3) << 0, 0, 0);
  bool is_white = norm.dot(point - center) == 0.0;
  return !is_white ? ((center - (norm.dot(center) / norm.dot(point - center)) * (point - center)) * transformation.t()) : default_proj;
}

cv::Mat ProjOnLab(cv::Mat rgb) {
  const cv::Mat kWhite = (cv::Mat_<double>(1, 3) << 1.0, 1.0, 1.0);
  const cv::Mat kNorm = (cv::Mat_<double>(1, 3) << 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0));
  const cv::Mat kRgbToLab = (cv::Mat_<double>(3, 3) <<
      -1.0 / std::sqrt(2.0), +1.0 / std::sqrt(2.0), -0.0,
      +1.0 / std::sqrt(6.0), +1.0 / std::sqrt(6.0), -2.0 / std::sqrt(6.0),
      +1.0 / std::sqrt(3.0), +1.0 / std::sqrt(3.0), +1.0 / std::sqrt(3.0)
  );

  rgb.convertTo(rgb, CV_64FC3, 1.0 / 255.0);

  cv::Mat proj = ProjOnPlane(rgb, kWhite, kNorm, kRgbToLab);

  proj.convertTo(proj, CV_32SC1, 255.0);

  return proj;
}

int RadToDeg(double rad) {
  return std::lround((rad * 180.0 / CV_PI) + 360.0) % 360;
}

std::vector<int> FindExtremes(const cv::Mat& hist) {
  std::vector<int> extremes;

  int curr_delta = 0;
  int prev_delta = hist.at<int>(0) - hist.at<int>(hist.cols - 1);
  for (const auto& i : std::views::iota(0, hist.cols)) {
    curr_delta = hist.at<int>((i + 1) % hist.cols) - hist.at<int>(i);

    if (prev_delta != 0 && curr_delta == 0) {
      int j = i;
      while (hist.at<int>(++j % hist.cols) == hist.at<int>(i)) {}
      int next_delta = hist.at<int>(j % hist.cols) - hist.at<int>(i);

      if (prev_delta * next_delta < 0) {
        int mid = ((i + j) / 2) % hist.cols;
        extremes.push_back(mid);
      }

    } else if (prev_delta * curr_delta < 0) {
      extremes.push_back(i);
    }

    prev_delta = curr_delta;
  }
  std::ranges::sort(extremes);

  if (hist.at<int>(extremes[0]) > hist.at<int>(extremes[1])) {
    std::ranges::rotate(extremes, extremes.begin() + 1);
  }

  return extremes;
}

std::vector<int> FindPeaks(const cv::Mat& hist, int min_h) {
  std::vector<int> peaks;
  std::vector<int> extremes = FindExtremes(hist);

  for (const auto& peak_idx : std::views::iota(1u, extremes.size()) | std::views::stride(2)) {
    int lh = hist.at<int>(extremes[peak_idx]) - hist.at<int>(extremes[(peak_idx - 1) % extremes.size()]);
    int rh = hist.at<int>(extremes[peak_idx]) - hist.at<int>(extremes[(peak_idx + 1) % extremes.size()]);

    if (std::min(lh, rh) >= min_h) {
      peaks.push_back(extremes[peak_idx]);
    }
  }
  std::ranges::sort(peaks);

  return peaks;
}

double ComputeIou(const cv::Mat& predicted_mask, const cv::Mat& truth_mask) {
  double intersection_area = cv::countNonZero(predicted_mask & truth_mask);
  double union_area = cv::countNonZero(predicted_mask | truth_mask);

  return intersection_area / union_area;
}

double ComputePq(const std::vector<cv::Mat>& predicted_masks, const std::vector<cv::Mat>& truth_masks) {
  double sum_iou = 0.0;
  double tp = 0.0;

  std::vector<bool> matched_predicted_masks(predicted_masks.size(), false);
  std::vector<bool> matched_truth_masks(truth_masks.size(), false);

  for (const auto& [predicted_mask_idx, predicted_mask] : predicted_masks | std::views::enumerate) {
    double max_iou = 0.0;
    std::size_t max_iou_idx = -1;

    for (const auto& [truth_mask_idx, truth_mask] : truth_masks | std::views::enumerate) {
      double iou = ComputeIou(predicted_mask, truth_mask);

      if (iou >= max_iou) {
        max_iou = iou;
        max_iou_idx = truth_mask_idx;
      }
    }

    if (max_iou >= 0.5) {
      sum_iou += max_iou;
      ++tp;

      matched_predicted_masks[predicted_mask_idx] = true;
      matched_truth_masks[max_iou_idx] = true;
    }
  }

  double fp = static_cast<double>(std::ranges::count(matched_predicted_masks, false));
  double fn = static_cast<double>(std::ranges::count(matched_truth_masks, false));

  return sum_iou / (tp + 0.5 * (fp + fn));
}

}  // namespace doc_color_decomposer
