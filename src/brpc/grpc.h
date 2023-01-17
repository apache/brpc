// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#ifndef BRPC_GRPC_H
#define BRPC_GRPC_H

#include <map>
#include <brpc/http2.h>

namespace brpc {

enum GrpcStatus {
    // OK is returned on success.
    GRPC_OK = 0,

    // CANCELED indicates the operation was canceled (typically by the caller).
    GRPC_CANCELED,

    // Unknown error. An example of where this error may be returned is
    // if a Status value received from another address space belongs to
    // an error-space that is not known in this address space. Also
    // errors raised by APIs that do not return enough error information
    // may be converted to this error.
    GRPC_UNKNOWN,

    // INVALIDARGUMENT Indicates client specified an invalid argument.
    // Note that this differs from FAILEDPRECONDITION. It indicates arguments
    // that are problematic regardless of the state of the system
    // (e.g., a malformed file name).
    GRPC_INVALIDARGUMENT,

    // DEADLINEEXCEEDED Means operation expired before completion.
    // For operations that change the state of the system, this error may be
    // returned even if the operation has completed successfully. For
    // example, a successful response from a server could have been delayed
    // long enough for the deadline to expire.
    GRPC_DEADLINEEXCEEDED,

    // NOTFOUND Means some requested entity (e.g., file or directory) was
    // not found.
    GRPC_NOTFOUND,

    // ALREADYEXISTS Means an attempt to create an entity failed because one
    // already exists.
    GRPC_ALREADYEXISTS,

    // PERMISSIONDENIED Indicates the caller does not have permission to
    // execute the specified operation. It must not be used for rejections
    // caused by exhausting some resource (use ResourceExhausted
    // instead for those errors). It must not be
    // used if the caller cannot be identified (use UNAUTHENTICATED
    // instead for those errors).
    GRPC_PERMISSIONDENIED,

    // RESOURCEEXHAUSTED Indicates some resource has been exhausted, perhaps
    // a per-user quota, or perhaps the entire file system is out of space.
    GRPC_RESOURCEEXHAUSTED,

    // FAILEDPRECONDITION indicates operation was rejected because the
    // system is not in a state required for the operation's execution.
    // For example, directory to be deleted may be non-empty, an rmdir
    // operation is applied to a non-directory, etc.
    //
    // A litmus test that may help a service implementor in deciding
    // between FAILEDPRECONDITION, Aborted, and Unavailable:
    //  (a) Use Unavailable if the client can retry just the failing call.
    //  (b) Use Aborted if the client should retry at a higher-level
    //      (e.g., restarting a read-modify-write sequence).
    //  (c) Use FAILEDPRECONDITION if the client should not retry until
    //      the system state has been explicitly fixed. E.g., if an "rmdir"
    //      fails because the directory is non-empty, FAILEDPRECONDITION
    //      should be returned since the client should not retry unless
    //      they have first fixed up the directory by deleting files from it.
    //  (d) Use FAILEDPRECONDITION if the client performs conditional
    //      REST Get/Update/Delete on a resource and the resource on the
    //      server does not match the condition. E.g., conflicting
    //      read-modify-write on the same resource.
    GRPC_FAILEDPRECONDITION,

    // ABORTED indicates the operation was aborted, typically due to a
    // concurrency issue like sequencer check failures, transaction aborts,
    // etc.
    //
    // See litmus test above for deciding between FAILEDPRECONDITION,
    // Aborted, and Unavailable.
    GRPC_ABORTED,

    // OUTOFRANGE means operation was attempted past the valid range.
    // E.g., seeking or reading past end of file.
    //
    // Unlike INVALIDARGUMENT, this error indicates a problem that may
    // be fixed if the system state changes. For example, a 32-bit file
    // system will generate INVALIDARGUMENT if asked to read at an
    // offset that is not in the range [0,2^32-1], but it will generate
    // OUTOFRANGE if asked to read from an offset past the current
    // file size.
    //
    // There is a fair bit of overlap between FAILEDPRECONDITION and
    // OUTOFRANGE. We recommend using OUTOFRANGE (the more specific
    // error) when it applies so that callers who are iterating through
    // a space can easily look for an OUTOFRANGE error to detect when
    // they are done.
    GRPC_OUTOFRANGE,

    // UNIMPLEMENTED indicates operation is not implemented or not
    // supported/enabled in this service.
    GRPC_UNIMPLEMENTED,

    // INTERNAL errors. Means some invariants expected by underlying
    // system has been broken. If you see one of these errors,
    // something is very broken.
    GRPC_INTERNAL,

    // UNAVAILABLE indicates the service is currently unavailable.
    // This is a most likely a transient condition and may be corrected
    // by retrying with a backoff.
    //
    // See litmus test above for deciding between FAILEDPRECONDITION,
    // ABORTED, and UNAVAILABLE.
    GRPC_UNAVAILABLE,

    // DATALOSS indicates unrecoverable data loss or corruption.
    GRPC_DATALOSS,

    // UNAUTHENTICATED indicates the request does not have valid
    // authentication credentials for the operation.
    GRPC_UNAUTHENTICATED,

    GRPC_MAX,
}; 

// Get description of the error.
const char* GrpcStatusToString(GrpcStatus);

// Convert between error code and grpc status with similar semantics
GrpcStatus ErrorCodeToGrpcStatus(int error_code);
int GrpcStatusToErrorCode(GrpcStatus grpc_status);

void PercentEncode(const std::string& str, std::string* str_out);

void PercentDecode(const std::string& str, std::string* str_out);


} // namespace brpc

#endif // BRPC_GRPC_H
