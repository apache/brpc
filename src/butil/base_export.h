// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_BASE_EXPORT_H_
#define BUTIL_BASE_EXPORT_H_

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(BUTIL_IMPLEMENTATION)
#define BUTIL_EXPORT __declspec(dllexport)
#define BUTIL_EXPORT_PRIVATE __declspec(dllexport)
#else
#define BUTIL_EXPORT __declspec(dllimport)
#define BUTIL_EXPORT_PRIVATE __declspec(dllimport)
#endif  // defined(BUTIL_IMPLEMENTATION)

#else  // defined(WIN32)
#if defined(BUTIL_IMPLEMENTATION)
#define BUTIL_EXPORT __attribute__((visibility("default")))
#define BUTIL_EXPORT_PRIVATE __attribute__((visibility("default")))
#else
#define BUTIL_EXPORT
#define BUTIL_EXPORT_PRIVATE
#endif  // defined(BUTIL_IMPLEMENTATION)
#endif

#else  // defined(COMPONENT_BUILD)
#define BUTIL_EXPORT
#define BUTIL_EXPORT_PRIVATE
#endif

#endif  // BUTIL_BASE_EXPORT_H_
