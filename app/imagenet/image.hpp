#pragma once

#include <fstream>
#include <opencv2/opencv.hpp>

namespace imagenet {

struct RawImage {
  RawImage() = default;
  RawImage(std::string path);
  std::vector<char> data;

  template <class Archive>
  void save(Archive &ar) const {
    ar(data);
  }

  template <class Archive>
  void load(Archive &ar) {
    ar(data);
  }
};

struct Image {
  Image() = default;
  Image(std::vector<char> vec);
  Image(cv::Mat mat);
  std::vector<char> data;

  template <class Archive>
  void save(Archive &ar) const {
    ar(data);
  }

  template <class Archive>
  void load(Archive &ar) {
    ar(data);
  }
};

Image kernel(RawImage image);

}  // namespace imagenet
