--
-- Licensed to the Apache Software Foundation (ASF) under one
-- or more contributor license agreements. See the NOTICE file
-- distributed with this work for additional information
-- regarding copyright ownership. The ASF licenses this file
-- to you under the Apache License, Version 2.0 (the
-- "License"); you may not use this file except in compliance
-- with the License. You may obtain a copy of the License at
--
--   http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing,
-- software distributed under the License is distributed on an
-- "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
-- KIND, either express or implied. See the License for the
-- specific language governing permissions and limitations
-- under the License.
--

--------------------------------------------------------------------------------
--  function
--  Proto.new(name, desc)
--  proto.dissector
--  proto.fields
--  proto.prefs
--  proto.prefs_changed
--  proto.init
--  proto.name
--  proto.description
--------------------------------------------------------------------------------

local plugin_info = {
  name = "baidu",
  version = "0.1.0",
  description = "baidu_std"
}

local major, minor, patch = get_version():match("(%d+)%.(%d+)%.(%d+)")
local version_code = tonumber(major) * 1000 * 1000 + tonumber(minor) * 1000 + tonumber(patch)

local proto = Proto.new(plugin_info.name, plugin_info.description)

local LM_DBG = function(fmt, ...)
  if (tonumber(major) < 3) then
    critical(table.concat({ plugin_info.name:upper() .. ":", fmt }, ' '):format(...))
  else
    print   (table.concat({ plugin_info.name:upper() .. ":", fmt }, ' '):format(...))
  end
end

LM_DBG("Wireshark version =", get_version())
LM_DBG("Lua version =", _VERSION)

--------------------------------------------------------------------------------

local MAGIC_CODE_PRPC = "PRPC"
local MAGIC_CODE_STRM = "STRM"

local PROTO_HEADER_LENGTH = 12


local pf_magic     = ProtoField.string(plugin_info.name .. ".magic",
                                       "Magic Code", BASE_NONE)
local pf_body_size = ProtoField.uint32(plugin_info.name .. ".body_size",
                                       "Body Size", BASE_DEC)
local pf_meta_size = ProtoField.uint32(plugin_info.name .. ".meta_size",
                                       "Meta Size", BASE_DEC)

-- used to customize tree item
local pf_any       = ProtoField.bytes (plugin_info.name .. ".any",
                                       "Any", BASE_NONE)

proto.fields = {
  pf_magic,
  pf_body_size,
  pf_meta_size,
  pf_any
}

----------------------------------------
-- ref fields
local proto_f_magic_code = Field.new(plugin_info.name .. ".magic")
local proto_f_body_size  = Field.new(plugin_info.name .. ".body_size")
local proto_f_meta_size  = Field.new(plugin_info.name .. ".meta_size")

local proto_f_protobuf_field_name  = Field.new("protobuf.field.name")
local proto_f_protobuf_field_value = Field.new("protobuf.field.value")

----------------------------------------
-- protobuf dissector
-- Note:
--   options.proto, baidu_rpc_meta.proto and streaming_rpc_meta.proto
--   should be put in Wireshark Protobuf Search Paths.
local protobuf_dissector = Dissector.get("protobuf")

----------------------------------------
-- declare functions

local check_length  = function() end
local dissect_proto = function() end

----------------------------------------
-- main dissector
function proto.dissector(tvbuf, pktinfo, root)
    local pktlen = tvbuf:len()

    local bytes_consumed = 0

    while bytes_consumed < pktlen do
        local result = dissect_proto(tvbuf, pktinfo, root, bytes_consumed)

        if result > 0 then
            bytes_consumed = bytes_consumed + result
        elseif result == 0 then
            -- hit an error
            return 0
        else
            pktinfo.desegment_offset = bytes_consumed
            -- require more bytes
            pktinfo.desegment_len = -result

            return pktlen
        end
    end

    return bytes_consumed
end

--------------------------------------------------------------------------------
-- heuristic
local function heur_dissect_proto(tvbuf, pktinfo, root)
    LM_DBG("heur brpc: pkg number: " .. pktinfo.number)
    if (tvbuf:len() < PROTO_HEADER_LENGTH) then
        LM_DBG("too short: pkg number: " .. pktinfo.number)
        return false
    end

    local magic = tvbuf:range(0, 4):string()
    -- for range dissectors
    if magic ~= MAGIC_CODE_PRPC and maigc ~= MAGIC_CODE_STRM then
        LM_DBG("invalid magic code: pkg number: " .. pktinfo.number)
        return false
    end

    proto.dissector(tvbuf, pktinfo, root)

    pktinfo.conversation = proto

    return true
end
proto:register_heuristic("tcp", heur_dissect_proto)

--------------------------------------------------------------------------------

local correlation_method_map = {}

local store_method = function(correlation_id, method)
    -- TODO: pop items
    correlation_method_map[correlation_id] = method
end

local load_method = function(correlation_id)
    return correlation_method_map[correlation_id]
end

-- check packet length, return length of packet if valid
check_length = function(tvbuf, offset)
    local msglen = tvbuf:len() - offset

    if msglen ~= tvbuf:reported_length_remaining(offset) then
        -- captured packets are being sliced/cut-off, so don't try to desegment/reassemble
        LM_WARN("Captured packet was shorter than original, can't reassemble")
        return 0
    end

    if msglen < PROTO_HEADER_LENGTH then
        -- we need more bytes, so tell the main dissector function that we
        -- didn't dissect anything, and we need an unknown number of more
        -- bytes (which is what "DESEGMENT_ONE_MORE_SEGMENT" is used for)
        return -DESEGMENT_ONE_MORE_SEGMENT
    end

    -- if we got here, then we know we have enough bytes in the Tvb buffer
    -- to at least figure out whether this is valid baidu_std packet

    local magic = tvbuf:range(offset, 4):string()
    if magic ~= MAGIC_CODE_PRPC and magic ~= MAGIC_CODE_STRM then
        return 0
    end

    -- big endian
    local packet_size = tvbuf:range(offset+4, 4):uint() + PROTO_HEADER_LENGTH
    if msglen < packet_size then
        LM_DBG("Need more bytes to desegment full baidu_std packet")
        return -(packet_size - msglen)
    end

    return packet_size
end

----------------------------------------
dissect_proto = function(tvbuf, pktinfo, root, offset)
    local len = check_length(tvbuf, offset)
    if len <= 0 then
        return len
    end

    local tree = root:add(proto, tvbuf:range(offset, len))

    tree:add(pf_magic,     tvbuf:range(offset,   4))
    tree:add(pf_body_size, tvbuf:range(offset+4, 4))
    tree:add(pf_meta_size, tvbuf:range(offset+8, 4))

    local meta_size = proto_f_meta_size().value
    local body_size = proto_f_body_size().value
    local tvb_meta = tvbuf:range(offset + PROTO_HEADER_LENGTH, meta_size):tvb()
    if     proto_f_magic_code().value == MAGIC_CODE_PRPC then
        -- dissect rpc meta fields
        pktinfo.private["pb_msg_type"] = "message,brpc.policy.RpcMeta"
        protobuf_dissector:call(tvb_meta, pktinfo, tree)

        local direction, method

        local service_name, method_name, correlation_id

        -- https://ask.wireshark.org/question/31800/protobuf-dissector-with-nested-structures/?answer=31924#post-id-31924
        local protobuf_field_names  = { proto_f_protobuf_field_name() }
        local protobuf_field_values = { proto_f_protobuf_field_value() }
        for k, v in pairs(protobuf_field_names) do
            if     v.value == "request" then
                direction = "request"
            elseif v.value == "response" then
                direction = "response"
            elseif v.value == "service_name" then
                service_name = protobuf_field_values[k].range:string(ENC_UTF8)
            elseif v.value == "method_name" then
                method_name = protobuf_field_values[k].range:string(ENC_UTF8)
            elseif v.value == "correlation_id" then
                correlation_id = protobuf_field_values[k].range:uint64()
            end
        end

        if     direction == "request" then
            method = service_name .. "/" .. method_name
            -- NOTE: convert uint64 to string to be used as key of table
            store_method(tostring(correlation_id), method)
        elseif direction == "response" then
            method = load_method(tostring(correlation_id))
        end

        if method ~= nil then
            -- dissect rpc body
            local tvb_body = tvbuf:range(offset + PROTO_HEADER_LENGTH + meta_size, body_size - meta_size):tvb()
            pktinfo.private["pb_msg_type"] = "application/grpc,/" .. method .. "," .. direction
            protobuf_dissector:call(tvb_body, pktinfo, tree)
        end
    elseif proto_f_magic_code().value == MAGIC_CODE_STRM then
        -- dissect streaming meta fields
        pktinfo.private["pb_msg_type"] = "message,brpc.StreamFrameMeta"
        protobuf_dissector:call(tvb_meta, pktinfo, tree)
    end

    -- body fields are business related, customized here

    return len
end

--------------------------------------------------------------------------------
-- Editor modelines
--
-- Local variables:
-- c-basic-offset: 4
-- tab-width: 4
-- indent-tab-mode: nil
-- End:
--
-- kate: indent-width 4; tab-width 4;
-- vim: tabstop=4:softtabstop=4:shiftwidth=4:expandtab
-- :indentSize=4:tabSize=4:noTabs=true
