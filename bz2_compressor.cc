// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bz2_compressor.h"

#include <string.h>

#include "logging.h"

using std::endl;

namespace {

// The BZ2 compression level used. Smaller compression levels are nowadays
// pointless.
const int kCompressionLevel = 9;

}  // namespace

namespace bsdiff {

BZ2Compressor::BZ2Compressor() {
  memset(&bz_strm_, 0, sizeof(bz_strm_));
  int bz_err = BZ2_bzCompressInit(&bz_strm_, kCompressionLevel,
                                  0 /* verbosity */, 0 /* workFactor */);
  if (bz_err != BZ_OK) {
    LOG(ERROR) << "Initializing bz_strm, bz_error=" << bz_err << endl;
  } else {
    bz_strm_initialized_ = true;
  }
  comp_buffer_.resize(1024 * 1024);
}

BZ2Compressor::~BZ2Compressor() {
  if (!bz_strm_initialized_)
    return;
  int bz_err = BZ2_bzCompressEnd(&bz_strm_);
  if (bz_err != BZ_OK) {
    LOG(ERROR) << "Deleting the compressor stream, bz_error=" << bz_err << endl;
  }
}

bool BZ2Compressor::Write(const uint8_t* buf, size_t size) {
  if (!bz_strm_initialized_)
    return false;

  // The bz_stream struct defines the next_in as a non-const data pointer,
  // although the documentation says it won't modify it.
  bz_strm_.next_in = reinterpret_cast<char*>(const_cast<uint8_t*>(buf));
  bz_strm_.avail_in = size;

  while (bz_strm_.avail_in) {
    bz_strm_.next_out = reinterpret_cast<char*>(comp_buffer_.data());
    bz_strm_.avail_out = comp_buffer_.size();
    int bz_err = BZ2_bzCompress(&bz_strm_, BZ_RUN);
    if (bz_err != BZ_RUN_OK) {
      LOG(ERROR) << "Compressing data, bz_error=" << bz_err << endl;
      return false;
    }

    uint64_t output_bytes = comp_buffer_.size() - bz_strm_.avail_out;
    if (output_bytes) {
      comp_chunks_.emplace_back(comp_buffer_.data(),
                                comp_buffer_.data() + output_bytes);
    }
  }
  return true;
}

bool BZ2Compressor::Finish() {
  if (!bz_strm_initialized_)
    return false;
  bz_strm_.next_in = nullptr;
  bz_strm_.avail_in = 0;

  int bz_err = BZ_FINISH_OK;
  while (bz_err == BZ_FINISH_OK) {
    bz_strm_.next_out = reinterpret_cast<char*>(comp_buffer_.data());
    bz_strm_.avail_out = comp_buffer_.size();
    bz_err = BZ2_bzCompress(&bz_strm_, BZ_FINISH);

    uint64_t output_bytes = comp_buffer_.size() - bz_strm_.avail_out;
    if (output_bytes) {
      comp_chunks_.emplace_back(comp_buffer_.data(),
                                comp_buffer_.data() + output_bytes);
    }
  }
  if (bz_err != BZ_STREAM_END) {
    LOG(ERROR) << "Finishing compressing data, bz_error=" << bz_err << endl;
    return false;
  }
  bz_err = BZ2_bzCompressEnd(&bz_strm_);
  bz_strm_initialized_ = false;
  if (bz_err != BZ_OK) {
    LOG(ERROR) << "Deleting the compressor stream, bz_error=" << bz_err << endl;
    return false;
  }
  return true;
}

const std::vector<uint8_t>& BZ2Compressor::GetCompressedData() {
  if (!comp_chunks_.empty()) {
    size_t chunks_size = 0;
    for (const auto& chunk : comp_chunks_)
      chunks_size += chunk.size();
    comp_data_.reserve(comp_data_.size() + chunks_size);
    for (const auto& chunk : comp_chunks_) {
      comp_data_.insert(comp_data_.end(), chunk.begin(), chunk.end());
    }
    comp_chunks_.clear();
  }
  return comp_data_;
}

}  // namespace bsdiff
