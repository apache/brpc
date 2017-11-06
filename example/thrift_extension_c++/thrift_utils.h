// Copyright (c) 2017 Baidu, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// utils for serilize/deserilize thrift binary message to thrift obj.

#include "thrift/transport/TBufferTransports.h"
#include "thrift/protocol/TBinaryProtocol.h"

template <typename THRIFT_ARG, typename THRIFT_REQ>
bool serilize_thrift_client_message(const brpc::ThriftBinaryMessage& request,
	THRIFT_REQ* thrift_request, std::string* function_name, int32_t* seqid) {
	
	boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> buffer(
		new apache::thrift::transport::TMemoryBuffer());
	boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> iprot(
		new apache::thrift::protocol::TBinaryProtocol(buffer));

	size_t body_len  = request.head.body_len;
	uint8_t* thrift_buffer = (uint8_t*)malloc(body_len);
	const size_t k = request.body.copy_to(thrift_buffer, body_len);
	if ( k != body_len) {
		free(thrift_buffer);
		return false;
	}

	THRIFT_ARG args;
	buffer->resetBuffer(thrift_buffer, body_len);
	apache::thrift::protocol::TMessageType mtype;

	// deserilize thrift message
	iprot->readMessageBegin(*function_name, mtype, *seqid);

	args.read(iprot.get());
	iprot->readMessageEnd();
	iprot->getTransport()->readEnd();

	*thrift_request = args.request;

	free(thrift_buffer);
	return true;
}

template <typename THRIFT_ARG, typename THRIFT_RES>
bool deserilize_thrift_client_message(const THRIFT_RES& thrift_response,
	const std::string& function_name, const int32_t seqid, brpc::ThriftBinaryMessage* response) {

	boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> o_buffer(
		new apache::thrift::transport::TMemoryBuffer());
	boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> oprot(
		new apache::thrift::protocol::TBinaryProtocol(o_buffer));

	THRIFT_ARG result;
	result.success = thrift_response;
	result.__isset.success = true;

	// serilize response
	oprot->writeMessageBegin(function_name, ::apache::thrift::protocol::T_REPLY, seqid);
	result.write(oprot.get());
	oprot->writeMessageEnd();
	oprot->getTransport()->writeEnd();
	oprot->getTransport()->flush();

	butil::IOBuf buf;
	std::string s = o_buffer->getBufferAsString();
	buf.append(s);

	response->body = buf;

	return true;
}

template <typename THRIFT_ARG, typename THRIFT_REQ>
bool serilize_thrift_server_message(const THRIFT_REQ& thrift_request,
	const std::string& function_name, const int32_t seqid, brpc::ThriftBinaryMessage* request) {
	
	boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> o_buffer(
		new apache::thrift::transport::TMemoryBuffer());
	boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> oprot(
		new apache::thrift::protocol::TBinaryProtocol(o_buffer));

	oprot->writeMessageBegin(function_name, apache::thrift::protocol::T_CALL, seqid);

	THRIFT_ARG args;
	args.request = &thrift_request;
	args.write(oprot.get());

	oprot->writeMessageEnd();
	oprot->getTransport()->writeEnd();
	oprot->getTransport()->flush();

	butil::IOBuf buf;
	std::string s = o_buffer->getBufferAsString();

	buf.append(s);
	request->body = buf;
	
	return true;
}

template<typename THRIFT_ARG, typename THRIFT_RES>
bool deserilize_thrift_server_message(const brpc::ThriftBinaryMessage& response,
	std::string* function_name, int32_t* seqid, THRIFT_RES* thrift_response) {
	
	boost::shared_ptr<apache::thrift::transport::TMemoryBuffer> buffer(
		new apache::thrift::transport::TMemoryBuffer());
	boost::shared_ptr<apache::thrift::protocol::TBinaryProtocol> iprot(
		new apache::thrift::protocol::TBinaryProtocol(buffer));

	size_t body_len  = response.head.body_len;
	uint8_t* thrift_buffer = (uint8_t*)malloc(body_len);
	const size_t k = response.body.copy_to(thrift_buffer, body_len);
	if ( k != body_len) {
		free(thrift_buffer);
		return false;
	}

	buffer->resetBuffer(thrift_buffer, body_len);

	apache::thrift::protocol::TMessageType mtype;

	try {
		iprot->readMessageBegin(*function_name, mtype, *seqid);
		
		THRIFT_ARG result;
		result.success = thrift_response;
		result.read(iprot.get());
		iprot->readMessageEnd();
		iprot->getTransport()->readEnd();

		if (!result.__isset.success) {
			free(thrift_buffer);
			return false;
		}
	} catch (...) {
		free(thrift_buffer);
		return false;
	}

	free(thrift_buffer);
	return true;

}
