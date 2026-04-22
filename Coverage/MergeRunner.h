#pragma once

#include "RuntimeOptions.h"

#include <cassert>
#include <memory>

class IMergeRunner
{
public:
  virtual void merge(const std::string& mergedFile, const std::string& outputFile) = 0;
};

class MergeRunner
{
public:
  /// Constructor
  /// \param[in] opts: application option. Need MergedOutput and OutputFile valid and defined + ExportFormat MUST BE Native.
  explicit MergeRunner(const RuntimeOptions& opts);

  // Avoid copy constructor
  MergeRunner(const MergeRunner&) = delete;

  /// Run merge
  void execute();
private:
  const RuntimeOptions& options;
  std::unique_ptr<IMergeRunner> runner;
};