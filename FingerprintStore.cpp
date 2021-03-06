#include "DirectoryWalker.hpp"
#include "Util.hpp"
#include <boost/filesystem.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "FingerprintStore.hpp"

FingerprintStore::FingerprintStore(std::string srcDirectory)
    : SrcDirectory(srcDirectory){};

void FingerprintStore::Load() {
  // Start iteration through all files in the directory
  DirectoryWalker dw(SrcDirectory);
  dw.Traverse(true);

  std::cout << "Loading fingerprints into memory..." << std::endl;
  int loadedCount = 0;

  while (true) {
    auto next = dw.GetNext();
    std::optional<boost::filesystem::path> entry = next.first;
    bool completed = next.second;

    // No next value as the directory traversal has completed.
    if (!entry.has_value() && completed)
      break;

    // We may not have an entry, but if directory traversal hasn't completed, we
    // should wait for the next one.
    if (!entry.has_value() && !completed) {
      sleep(1);
      continue;
    }

    // Filter only known image suffixes
    if (!Util::IsSupportedImage(entry.value()))
      continue;

    auto filename = entry.value().string();
    Magick::Image image;

    image.read(filename);
    Fingerprints.push_back(std::pair(image, entry.value().stem().string()));
    loadedCount++;
    std::stringstream msg;
    msg << "\r" << loadedCount;
    std::cout << msg.str() << std::flush;
  }

  // Wait also on the directory traversal thread to complete.
  dw.Finish();
  std::cout << "\rDONE\n" << std::flush;
}

void FingerprintStore::FindMatchesForImage(Magick::Image image,
                                           const std::string filename,
                                           const int fuzzFactor) {
  for (std::vector<std::pair<Magick::Image, std::string>>::iterator it =
           Fingerprints.begin();
       it != Fingerprints.end(); ++it) {

    // Compare the image to the fingerprint for total number of non-matching
    // pixels. Distortion will be the pixel count. 100x100 gives a minimum of 0
    // and max of 10000.
    image.colorFuzz(fuzzFactor);
    auto distortion =
        image.compare(it->first, Magick::RootMeanSquaredErrorMetric);

    std::stringstream msg;
    msg << filename;

    // Pull the fingerprint match name from the fingerprint metadata if
    // available.
    std::string fingerprintName = it->first.attribute("comment");
    if (fingerprintName == "") {
      fingerprintName = it->second;
    }

    if (distortion < LowDistortionThreshold) {
      msg << "\tis identical to\t" << fingerprintName << std::endl;
      std::cout << msg.str() << std::flush;
      continue;
    }

    if (distortion < HighDistortionThreshold) {
      msg << "\tis similar to\t" << fingerprintName << std::endl;
      std::cout << msg.str() << std::flush;
      continue;
    }
  }
}

void FingerprintStore::RunWorkers(const WorkerOptions options) {
  // Start asynchronous traversal of directory.
  DirectoryWalker *dw;
  if (options.WType == GenerateWorker) {
    dw = new DirectoryWalker(SrcDirectory);
  } else {
    dw = new DirectoryWalker(options.DstDirectory);
  }
  dw->Traverse(true);

  // Spawn threads for the actual fingerprint generation
  std::vector<std::thread> threads;
  for (int i = 0; i < options.NumThreads; i++) {
    std::thread thread;

    // Use the power of filthy lambdas to start the things.
    switch (options.WType) {
    case GenerateWorker:
      thread = std::thread([=] { Generate(dw, options.DstDirectory); });
      break;
    case MetadataWorker:
      thread = std::thread([=] { ExtractMetadata(dw); });
      break;
    case FingerprintWorker:
      thread = std::thread([=] { FindDuplicates(dw, options.FuzzFactor); });
      break;
    }

    threads.push_back(std::move(thread));
  }

  // Wait for them to finish
  for (int i = 0; i < options.NumThreads; i++) {
    if (threads[i].joinable())
      threads[i].join();
  }

  // Wait also on the directory traversal thread to complete.
  dw->Finish();
  delete dw;
}

void FingerprintStore::FindDuplicates(DirectoryWalker *dw,
                                      const int fuzzFactor) {
  while (true) {
    auto next = dw->GetNext();
    std::optional<boost::filesystem::path> entry = next.first;
    bool completed = next.second;

    // No next value as the directory traversal has completed.
    if (!entry.has_value() && completed)
      break;

    // We may not have an entry, but if directory traversal hasn't completed, we
    // should wait for the next one.
    if (!entry.has_value() && !completed) {
      sleep(1);
      continue;
    }

    // Filter only known image suffixes
    if (!Util::IsSupportedImage(entry.value()))
      continue;

    // Read in one image, resize it to comparison specifications
    auto filename = entry.value().string();
    Magick::Image image;
    try {
      image.read(filename);
    } catch (const std::exception &e) {
      // silently skip unreadable file for the moment
      continue;
    }
    image.compressType(
        MagickCore::CompressionType::NoCompression); // may not be needed
    image.resize(FingerprintSpec);

    // Compare
    FindMatchesForImage(image, filename, fuzzFactor);
  }
}

void FingerprintStore::Generate(DirectoryWalker *dw,
                                const std::string dstDirectory) {
  boost::filesystem::path dest(dstDirectory);

  // Iterate through all files in the directory
  while (true) {
    auto next = dw->GetNext();
    std::optional<boost::filesystem::path> entry = next.first;
    bool completed = next.second;

    // No next value as the directory traversal has completed.
    if (!entry.has_value() && completed)
      break;

    // We may not have an entry, but if directory traversal hasn't completed, we
    // should wait for the next one.
    if (!entry.has_value() && !completed) {
      sleep(1);
      continue;
    }

    // Filter only known image suffixes
    if (!Util::IsSupportedImage(entry.value()))
      continue;

    std::stringstream msg;
    msg << entry.value().string() << std::endl;
    std::cout << msg.str() << std::flush;
    auto filename = entry.value().filename().replace_extension(
        ".tif"); // save fingerprints uncompressed
    Magick::Image image;

    try {
      // destination filename
      auto outputFilename = boost::filesystem::path(dest);
      outputFilename += filename;

      image.read(entry.value().string());
      image.defineValue("quantum", "format",
                        "floating-point"); // fix HDRI comparison issues
      image.depth(32);                     // also for the HDRI stuff
      image.compressType(
          MagickCore::CompressionType::NoCompression); // may not be needed
      image.resize(FingerprintSpec);
      image.attribute("comment", entry.value().string());
      image.write(outputFilename.string());
    } catch (const std::exception &e) {
      // Some already seen:
      // Magick::ErrorCorruptImage
      // Magick::ErrorMissingDelegate
      // Magick::ErrorCoder
      // Magick::WarningImage
      std::stringstream msg;
      msg << "skipping " << entry.value().string() << " " << e.what()
          << std::endl;
      std::cerr << msg.str() << std::flush;
    }
  }
}

void FingerprintStore::ExtractMetadata(DirectoryWalker *dw) {
  // Iterate through all files in the directory
  while (true) {
    auto next = dw->GetNext();
    std::optional<boost::filesystem::path> entry = next.first;
    bool completed = next.second;

    // No next value as the directory traversal has completed.
    if (!entry.has_value() && completed)
      break;

    // We may not have an entry, but if directory traversal hasn't completed, we
    // should wait for the next one.
    if (!entry.has_value() && !completed) {
      sleep(1);
      continue;
    }

    // Filter only known image suffixes
    if (!Util::IsSupportedImage(entry.value()))
      continue;

    try {
      Magick::Image image;
      std::string filename = entry.value().string();
      image.read(filename);
      std::string createdAt = image.attribute("exif:DateTimeOriginal");

      if (createdAt != "") {
        std::string timestamp = ConvertExifTimestamp(createdAt);
        std::stringstream msg;
        msg << filename << "\t" << timestamp << std::endl;
        std::cout << msg.str() << std::flush;
      }
    } catch (const std::exception &e) {
      // Some already seen:
      // Magick::ErrorCorruptImage
      // Magick::ErrorMissingDelegate
      // Magick::ErrorCoder
      // Magick::WarningImage
      // Don't bother printing anything as we might run into all kinds of files
      // we can't read.
    }
  }
}

std::string
FingerprintStore::ConvertExifTimestamp(const std::string timestamp) {
  // https://en.cppreference.com/w/cpp/io/manip/get_time
  // This is basically the example from the docs.
  std::tm t;
  std::istringstream ss(timestamp);
  ss >> std::get_time(&t, "%Y:%m:%d %H:%M:%S");

  // std::put_time requires an output stream to write to
  std::ostringstream output;
  output << std::put_time(&t, "%Y-%m-%d %H:%M:%S");
  return output.str();
}
