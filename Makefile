#COMAKE2 edit-mode: -*- Makefile -*-
####################64Bit Mode####################
ifeq ($(shell uname -m),x86_64)
CC=gcc
CXX=g++
CXXFLAGS=-O2 \
  -g \
  -pipe \
  -Wall \
  -W \
  -Werror \
  -fPIC \
  -fstrict-aliasing \
  -Wno-invalid-offsetof \
  -Wno-unused-parameter \
  -fno-omit-frame-pointer \
  -std=c++0x \
  -include \
  brpc/config.h
CFLAGS=-O2 \
  -g \
  -pipe \
  -Wall \
  -W \
  -Werror \
  -fPIC \
  -fstrict-aliasing \
  -Wno-unused-parameter \
  -fno-omit-frame-pointer
CPPFLAGS=-D__const__= \
  -DNDEBUG \
  -DUSE_SYMBOLIZE \
  -DNO_TCMALLOC \
  -D__STDC_FORMAT_MACROS \
  -D__STDC_LIMIT_MACROS \
  -D__STDC_CONSTANT_MACROS \
  -DBRPC_REVISION=\"$(shell \
  git \
  rev-parse \
  --short \
  HEAD)\"
INCPATH=-I.
DEP_INCPATH=-I../../../third-64/gflags \
  -I../../../third-64/gflags/include \
  -I../../../third-64/gflags/output \
  -I../../../third-64/gflags/output/include \
  -I../../../third-64/leveldb \
  -I../../../third-64/leveldb/include \
  -I../../../third-64/leveldb/output \
  -I../../../third-64/leveldb/output/include \
  -I../../../third-64/protobuf \
  -I../../../third-64/protobuf/include \
  -I../../../third-64/protobuf/output \
  -I../../../third-64/protobuf/output/include

#============ CCP vars ============
CCHECK=@ccheck.py
CCHECK_FLAGS=
PCLINT=@pclint
PCLINT_FLAGS=
CCP=@ccp.py
CCP_FLAGS=


#COMAKE UUID
COMAKE_MD5=278b132339335c67b6a9d91325f51a67  COMAKE


.PHONY:all
all:comake2_makefile_check idl_options.pb.cc brpc/builtin_service.pb.cc brpc/errno.pb.cc brpc/get_favicon.pb.cc brpc/get_js.pb.cc brpc/nshead_meta.pb.cc brpc/options.pb.cc brpc/rpc_dump.pb.cc brpc/rtmp.pb.cc brpc/span.pb.cc brpc/streaming_rpc_meta.pb.cc brpc/trackme.pb.cc brpc/policy/baidu_rpc_meta.pb.cc brpc/policy/hulu_pbrpc_meta.pb.cc brpc/policy/mongo.pb.cc brpc/policy/public_pbrpc_meta.pb.cc brpc/policy/sofa_pbrpc_meta.pb.cc libbase.a libbvar.a libbthread.a libjson2pb.a libmcpack2pb.a protoc-gen-mcpack libbrpc.a output/include 
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mall[0m']"
	@echo "make all done"

.PHONY:comake2_makefile_check
comake2_makefile_check:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mcomake2_makefile_check[0m']"
	#in case of error, update 'Makefile' by 'comake2'
	@echo "$(COMAKE_MD5)">comake2.md5
	@md5sum -c --status comake2.md5
	@rm -f comake2.md5

.PHONY:ccpclean
ccpclean:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mccpclean[0m']"
	@echo "make ccpclean done"

.PHONY:clean
clean:ccpclean
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mclean[0m']"
	rm -rf libbase.a
	rm -rf ./output/lib/libbase.a
	rm -rf libbvar.a
	rm -rf ./output/lib/libbvar.a
	rm -rf libbthread.a
	rm -rf ./output/lib/libbthread.a
	rm -rf libjson2pb.a
	rm -rf ./output/lib/libjson2pb.a
	rm -rf libmcpack2pb.a
	rm -rf ./output/lib/libmcpack2pb.a
	rm -rf protoc-gen-mcpack
	rm -rf ./output/bin/protoc-gen-mcpack
	rm -rf libbrpc.a
	rm -rf ./output/lib/libbrpc.a
	rm -rf output/include
	rm -rf base/third_party/dmg_fp/base_g_fmt.o
	rm -rf base/third_party/dmg_fp/base_dtoa_wrapper.o
	rm -rf base/third_party/dmg_fp/base_dtoa.o
	rm -rf base/third_party/dynamic_annotations/base_dynamic_annotations.o
	rm -rf base/third_party/icu/base_icu_utf.o
	rm -rf base/third_party/superfasthash/base_superfasthash.o
	rm -rf base/third_party/modp_b64/base_modp_b64.o
	rm -rf base/third_party/nspr/base_prtime.o
	rm -rf base/third_party/symbolize/base_demangle.o
	rm -rf base/third_party/symbolize/base_symbolize.o
	rm -rf base/third_party/xdg_mime/base_xdgmime.o
	rm -rf base/third_party/xdg_mime/base_xdgmimealias.o
	rm -rf base/third_party/xdg_mime/base_xdgmimecache.o
	rm -rf base/third_party/xdg_mime/base_xdgmimeglob.o
	rm -rf base/third_party/xdg_mime/base_xdgmimeicon.o
	rm -rf base/third_party/xdg_mime/base_xdgmimeint.o
	rm -rf base/third_party/xdg_mime/base_xdgmimemagic.o
	rm -rf base/third_party/xdg_mime/base_xdgmimeparent.o
	rm -rf base/third_party/xdg_user_dirs/base_xdg_user_dir_lookup.o
	rm -rf base/third_party/snappy/base_snappy-sinksource.o
	rm -rf base/third_party/snappy/base_snappy-stubs-internal.o
	rm -rf base/third_party/snappy/base_snappy.o
	rm -rf base/third_party/murmurhash3/base_murmurhash3.o
	rm -rf base/allocator/base_type_profiler_control.o
	rm -rf base/base_arena.o
	rm -rf base/base_at_exit.o
	rm -rf base/base_atomicops_internals_x86_gcc.o
	rm -rf base/base_barrier_closure.o
	rm -rf base/base_base_paths.o
	rm -rf base/base_base_paths_posix.o
	rm -rf base/base_base64.o
	rm -rf base/base_base_switches.o
	rm -rf base/base_big_endian.o
	rm -rf base/base_bind_helpers.o
	rm -rf base/base_build_time.o
	rm -rf base/base_callback_helpers.o
	rm -rf base/base_callback_internal.o
	rm -rf base/base_command_line.o
	rm -rf base/base_cpu.o
	rm -rf base/debug/base_alias.o
	rm -rf base/debug/base_asan_invalid_access.o
	rm -rf base/debug/base_crash_logging.o
	rm -rf base/debug/base_debugger.o
	rm -rf base/debug/base_debugger_posix.o
	rm -rf base/debug/base_dump_without_crashing.o
	rm -rf base/debug/base_proc_maps_linux.o
	rm -rf base/debug/base_stack_trace.o
	rm -rf base/debug/base_stack_trace_posix.o
	rm -rf base/base_environment.o
	rm -rf base/files/base_file.o
	rm -rf base/files/base_file_posix.o
	rm -rf base/files/base_file_enumerator.o
	rm -rf base/files/base_file_enumerator_posix.o
	rm -rf base/files/base_file_path.o
	rm -rf base/files/base_file_path_constants.o
	rm -rf base/files/base_memory_mapped_file.o
	rm -rf base/files/base_memory_mapped_file_posix.o
	rm -rf base/files/base_scoped_file.o
	rm -rf base/files/base_scoped_temp_dir.o
	rm -rf base/base_file_util.o
	rm -rf base/base_file_util_linux.o
	rm -rf base/base_file_util_posix.o
	rm -rf base/base_guid.o
	rm -rf base/base_guid_posix.o
	rm -rf base/base_hash.o
	rm -rf base/base_lazy_instance.o
	rm -rf base/base_location.o
	rm -rf base/base_md5.o
	rm -rf base/memory/base_aligned_memory.o
	rm -rf base/memory/base_ref_counted.o
	rm -rf base/memory/base_ref_counted_memory.o
	rm -rf base/memory/base_shared_memory_posix.o
	rm -rf base/memory/base_singleton.o
	rm -rf base/memory/base_weak_ptr.o
	rm -rf base/nix/base_mime_util_xdg.o
	rm -rf base/nix/base_xdg_util.o
	rm -rf base/base_path_service.o
	rm -rf base/posix/base_file_descriptor_shuffle.o
	rm -rf base/posix/base_global_descriptors.o
	rm -rf base/process/base_internal_linux.o
	rm -rf base/process/base_kill.o
	rm -rf base/process/base_kill_posix.o
	rm -rf base/process/base_launch.o
	rm -rf base/process/base_launch_posix.o
	rm -rf base/process/base_memory.o
	rm -rf base/process/base_memory_linux.o
	rm -rf base/process/base_process_handle_linux.o
	rm -rf base/process/base_process_handle_posix.o
	rm -rf base/process/base_process_info_linux.o
	rm -rf base/process/base_process_iterator.o
	rm -rf base/process/base_process_iterator_linux.o
	rm -rf base/process/base_process_linux.o
	rm -rf base/process/base_process_metrics.o
	rm -rf base/process/base_process_metrics_linux.o
	rm -rf base/process/base_process_metrics_posix.o
	rm -rf base/process/base_process_posix.o
	rm -rf base/base_rand_util.o
	rm -rf base/base_rand_util_posix.o
	rm -rf base/base_fast_rand.o
	rm -rf base/base_safe_strerror_posix.o
	rm -rf base/base_sha1_portable.o
	rm -rf base/strings/base_latin1_string_conversions.o
	rm -rf base/strings/base_nullable_string16.o
	rm -rf base/strings/base_safe_sprintf.o
	rm -rf base/strings/base_string16.o
	rm -rf base/strings/base_string_number_conversions.o
	rm -rf base/strings/base_string_split.o
	rm -rf base/strings/base_string_piece.o
	rm -rf base/strings/base_string_util.o
	rm -rf base/strings/base_string_util_constants.o
	rm -rf base/strings/base_stringprintf.o
	rm -rf base/strings/base_sys_string_conversions_posix.o
	rm -rf base/strings/base_utf_offset_string_conversions.o
	rm -rf base/strings/base_utf_string_conversion_utils.o
	rm -rf base/strings/base_utf_string_conversions.o
	rm -rf base/synchronization/base_cancellation_flag.o
	rm -rf base/synchronization/base_condition_variable_posix.o
	rm -rf base/synchronization/base_waitable_event_posix.o
	rm -rf base/base_sys_info.o
	rm -rf base/base_sys_info_linux.o
	rm -rf base/base_sys_info_posix.o
	rm -rf base/threading/base_non_thread_safe_impl.o
	rm -rf base/threading/base_platform_thread_linux.o
	rm -rf base/threading/base_platform_thread_posix.o
	rm -rf base/threading/base_simple_thread.o
	rm -rf base/threading/base_thread_checker_impl.o
	rm -rf base/threading/base_thread_collision_warner.o
	rm -rf base/threading/base_thread_id_name_manager.o
	rm -rf base/threading/base_thread_local_posix.o
	rm -rf base/threading/base_thread_local_storage.o
	rm -rf base/threading/base_thread_local_storage_posix.o
	rm -rf base/threading/base_thread_restrictions.o
	rm -rf base/threading/base_watchdog.o
	rm -rf base/time/base_clock.o
	rm -rf base/time/base_default_clock.o
	rm -rf base/time/base_default_tick_clock.o
	rm -rf base/time/base_tick_clock.o
	rm -rf base/time/base_time.o
	rm -rf base/time/base_time_posix.o
	rm -rf base/base_version.o
	rm -rf base/base_logging.o
	rm -rf base/base_class_name.o
	rm -rf base/base_errno.o
	rm -rf base/base_find_cstr.o
	rm -rf base/base_status.o
	rm -rf base/base_string_printf.o
	rm -rf base/base_thread_local.o
	rm -rf base/base_unix_socket.o
	rm -rf base/base_endpoint.o
	rm -rf base/base_fd_utility.o
	rm -rf base/files/base_temp_file.o
	rm -rf base/files/base_file_watcher.o
	rm -rf base/base_time.o
	rm -rf base/base_zero_copy_stream_as_streambuf.o
	rm -rf base/base_crc32c.o
	rm -rf base/containers/base_case_ignored_flat_map.o
	rm -rf base/base_iobuf.o
	rm -rf bvar/bvar_collector.o
	rm -rf bvar/bvar_default_variables.o
	rm -rf bvar/bvar_gflag.o
	rm -rf bvar/bvar_latency_recorder.o
	rm -rf bvar/bvar_variable.o
	rm -rf bvar/detail/bvar_percentile.o
	rm -rf bvar/detail/bvar_sampler.o
	rm -rf bthread/bthread_bthread.o
	rm -rf bthread/bthread_butex.o
	rm -rf bthread/bthread_cond.o
	rm -rf bthread/bthread_context.o
	rm -rf bthread/bthread_countdown_event.o
	rm -rf bthread/bthread_errno.o
	rm -rf bthread/bthread_execution_queue.o
	rm -rf bthread/bthread_fd.o
	rm -rf bthread/bthread_id.o
	rm -rf bthread/bthread_interrupt_pthread.o
	rm -rf bthread/bthread_key.o
	rm -rf bthread/bthread_mutex.o
	rm -rf bthread/bthread_stack.o
	rm -rf bthread/bthread_sys_futex.o
	rm -rf bthread/bthread_task_control.o
	rm -rf bthread/bthread_task_group.o
	rm -rf bthread/bthread_timer_thread.o
	rm -rf json2pb/json2pb_encode_decode.o
	rm -rf json2pb/json2pb_json_to_pb.o
	rm -rf json2pb/json2pb_pb_to_json.o
	rm -rf json2pb/json2pb_protobuf_map.o
	rm -rf mcpack2pb/mcpack2pb_field_type.o
	rm -rf mcpack2pb/mcpack2pb_mcpack2pb.o
	rm -rf mcpack2pb/mcpack2pb_parser.o
	rm -rf mcpack2pb/mcpack2pb_serializer.o
	rm -rf mcpack2pb_idl_options.pb.o
	rm -rf mcpack2pb/protoc-gen-mcpack_generator.o
	rm -rf brpc/brpc_acceptor.o
	rm -rf brpc/brpc_adaptive_connection_type.o
	rm -rf brpc/brpc_amf.o
	rm -rf brpc/brpc_bad_method_service.o
	rm -rf brpc/brpc_channel.o
	rm -rf brpc/brpc_compress.o
	rm -rf brpc/brpc_controller.o
	rm -rf brpc/brpc_esp_message.o
	rm -rf brpc/brpc_event_dispatcher.o
	rm -rf brpc/brpc_global.o
	rm -rf brpc/brpc_http_header.o
	rm -rf brpc/brpc_http_method.o
	rm -rf brpc/brpc_http_status_code.o
	rm -rf brpc/brpc_input_messenger.o
	rm -rf brpc/brpc_load_balancer.o
	rm -rf brpc/brpc_load_balancer_with_naming.o
	rm -rf brpc/brpc_memcache.o
	rm -rf brpc/brpc_naming_service_thread.o
	rm -rf brpc/brpc_nshead_message.o
	rm -rf brpc/brpc_nshead_pb_service_adaptor.o
	rm -rf brpc/brpc_nshead_service.o
	rm -rf brpc/brpc_parallel_channel.o
	rm -rf brpc/brpc_partition_channel.o
	rm -rf brpc/brpc_periodic_naming_service.o
	rm -rf brpc/brpc_progressive_attachment.o
	rm -rf brpc/brpc_protocol.o
	rm -rf brpc/brpc_redis.o
	rm -rf brpc/brpc_redis_command.o
	rm -rf brpc/brpc_redis_reply.o
	rm -rf brpc/brpc_reloadable_flags.o
	rm -rf brpc/brpc_restful.o
	rm -rf brpc/brpc_retry_policy.o
	rm -rf brpc/brpc_rpc_dump.o
	rm -rf brpc/brpc_rtmp.o
	rm -rf brpc/brpc_selective_channel.o
	rm -rf brpc/brpc_serialized_request.o
	rm -rf brpc/brpc_server.o
	rm -rf brpc/brpc_server_id.o
	rm -rf brpc/brpc_socket.o
	rm -rf brpc/brpc_socket_map.o
	rm -rf brpc/brpc_span.o
	rm -rf brpc/brpc_stream.o
	rm -rf brpc/brpc_tcmalloc_extension.o
	rm -rf brpc/brpc_trackme.o
	rm -rf brpc/brpc_ts.o
	rm -rf brpc/brpc_uri.o
	rm -rf brpc/policy/brpc_baidu_rpc_protocol.o
	rm -rf brpc/policy/brpc_consistent_hashing_load_balancer.o
	rm -rf brpc/policy/brpc_dh.o
	rm -rf brpc/policy/brpc_domain_naming_service.o
	rm -rf brpc/policy/brpc_dynpart_load_balancer.o
	rm -rf brpc/policy/brpc_esp_authenticator.o
	rm -rf brpc/policy/brpc_esp_protocol.o
	rm -rf brpc/policy/brpc_file_naming_service.o
	rm -rf brpc/policy/brpc_gzip_compress.o
	rm -rf brpc/policy/brpc_hasher.o
	rm -rf brpc/policy/brpc_http_rpc_protocol.o
	rm -rf brpc/policy/brpc_hulu_pbrpc_protocol.o
	rm -rf brpc/policy/brpc_list_naming_service.o
	rm -rf brpc/policy/brpc_locality_aware_load_balancer.o
	rm -rf brpc/policy/brpc_memcache_binary_protocol.o
	rm -rf brpc/policy/brpc_mongo_protocol.o
	rm -rf brpc/policy/brpc_nova_pbrpc_protocol.o
	rm -rf brpc/policy/brpc_nshead_mcpack_protocol.o
	rm -rf brpc/policy/brpc_nshead_protocol.o
	rm -rf brpc/policy/brpc_public_pbrpc_protocol.o
	rm -rf brpc/policy/brpc_randomized_load_balancer.o
	rm -rf brpc/policy/brpc_redis_protocol.o
	rm -rf brpc/policy/brpc_remote_file_naming_service.o
	rm -rf brpc/policy/brpc_round_robin_load_balancer.o
	rm -rf brpc/policy/brpc_rtmp_protocol.o
	rm -rf brpc/policy/brpc_snappy_compress.o
	rm -rf brpc/policy/brpc_sofa_pbrpc_protocol.o
	rm -rf brpc/policy/brpc_streaming_rpc_protocol.o
	rm -rf brpc/policy/brpc_ubrpc2pb_protocol.o
	rm -rf brpc/builtin/brpc_bthreads_service.o
	rm -rf brpc/builtin/brpc_common.o
	rm -rf brpc/builtin/brpc_connections_service.o
	rm -rf brpc/builtin/brpc_dir_service.o
	rm -rf brpc/builtin/brpc_flags_service.o
	rm -rf brpc/builtin/brpc_flot_min_js.o
	rm -rf brpc/builtin/brpc_get_favicon_service.o
	rm -rf brpc/builtin/brpc_get_js_service.o
	rm -rf brpc/builtin/brpc_health_service.o
	rm -rf brpc/builtin/brpc_hotspots_service.o
	rm -rf brpc/builtin/brpc_ids_service.o
	rm -rf brpc/builtin/brpc_index_service.o
	rm -rf brpc/builtin/brpc_jquery_min_js.o
	rm -rf brpc/builtin/brpc_list_service.o
	rm -rf brpc/builtin/brpc_pprof_perl.o
	rm -rf brpc/builtin/brpc_pprof_service.o
	rm -rf brpc/builtin/brpc_protobufs_service.o
	rm -rf brpc/builtin/brpc_rpcz_service.o
	rm -rf brpc/builtin/brpc_sockets_service.o
	rm -rf brpc/builtin/brpc_sorttable_js.o
	rm -rf brpc/builtin/brpc_status_service.o
	rm -rf brpc/builtin/brpc_threads_service.o
	rm -rf brpc/builtin/brpc_vars_service.o
	rm -rf brpc/builtin/brpc_version_service.o
	rm -rf brpc/builtin/brpc_viz_min_js.o
	rm -rf brpc/builtin/brpc_vlog_service.o
	rm -rf brpc/details/brpc_has_epollrdhup.o
	rm -rf brpc/details/brpc_hpack.o
	rm -rf brpc/details/brpc_http_message.o
	rm -rf brpc/details/brpc_http_message_serializer.o
	rm -rf brpc/details/brpc_http_parser.o
	rm -rf brpc/details/brpc_method_status.o
	rm -rf brpc/details/brpc_rtmp_utils.o
	rm -rf brpc/details/brpc_ssl_helper.o
	rm -rf brpc/details/brpc_usercode_backup_pool.o
	rm -rf brpc/brpc_builtin_service.pb.o
	rm -rf brpc/brpc_errno.pb.o
	rm -rf brpc/brpc_get_favicon.pb.o
	rm -rf brpc/brpc_get_js.pb.o
	rm -rf brpc/brpc_nshead_meta.pb.o
	rm -rf brpc/brpc_options.pb.o
	rm -rf brpc/brpc_rpc_dump.pb.o
	rm -rf brpc/brpc_rtmp.pb.o
	rm -rf brpc/brpc_span.pb.o
	rm -rf brpc/brpc_streaming_rpc_meta.pb.o
	rm -rf brpc/brpc_trackme.pb.o
	rm -rf brpc/policy/brpc_baidu_rpc_meta.pb.o
	rm -rf brpc/policy/brpc_hulu_pbrpc_meta.pb.o
	rm -rf brpc/policy/brpc_mongo.pb.o
	rm -rf brpc/policy/brpc_public_pbrpc_meta.pb.o
	rm -rf brpc/policy/brpc_sofa_pbrpc_meta.pb.o

.PHONY:dist
dist:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mdist[0m']"
	tar czvf output.tar.gz output
	@echo "make dist done"

.PHONY:distclean
distclean:clean
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mdistclean[0m']"
	rm -f output.tar.gz
	@echo "make distclean done"

.PHONY:love
love:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlove[0m']"
	@echo "make love done"

idl_options.pb.cc:./idl_options.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40midl_options.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./idl_options.proto

brpc/builtin_service.pb.cc:./brpc/builtin_service.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin_service.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/builtin_service.proto

brpc/errno.pb.cc:./brpc/errno.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/errno.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/errno.proto

brpc/get_favicon.pb.cc:./brpc/get_favicon.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/get_favicon.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/get_favicon.proto

brpc/get_js.pb.cc:./brpc/get_js.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/get_js.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/get_js.proto

brpc/nshead_meta.pb.cc:./brpc/nshead_meta.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/nshead_meta.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/nshead_meta.proto

brpc/options.pb.cc:./brpc/options.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/options.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/options.proto

brpc/rpc_dump.pb.cc:./brpc/rpc_dump.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/rpc_dump.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/rpc_dump.proto

brpc/rtmp.pb.cc:./brpc/rtmp.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/rtmp.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/rtmp.proto

brpc/span.pb.cc:./brpc/span.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/span.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/span.proto

brpc/streaming_rpc_meta.pb.cc:./brpc/streaming_rpc_meta.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/streaming_rpc_meta.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/streaming_rpc_meta.proto

brpc/trackme.pb.cc:./brpc/trackme.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/trackme.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/trackme.proto

brpc/policy/baidu_rpc_meta.pb.cc:./brpc/policy/baidu_rpc_meta.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/baidu_rpc_meta.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/policy/baidu_rpc_meta.proto

brpc/policy/hulu_pbrpc_meta.pb.cc:./brpc/policy/hulu_pbrpc_meta.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/hulu_pbrpc_meta.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/policy/hulu_pbrpc_meta.proto

brpc/policy/mongo.pb.cc:./brpc/policy/mongo.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/mongo.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/policy/mongo.proto

brpc/policy/public_pbrpc_meta.pb.cc:./brpc/policy/public_pbrpc_meta.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/public_pbrpc_meta.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/policy/public_pbrpc_meta.proto

brpc/policy/sofa_pbrpc_meta.pb.cc:./brpc/policy/sofa_pbrpc_meta.proto \
  ../../..//third-64/protobuf/bin/protoc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/sofa_pbrpc_meta.pb.cc[0m']"
	../../..//third-64/protobuf/bin/protoc --proto_path=. --proto_path=../../..//third-64/protobuf/include/ --cpp_out=. ./brpc/policy/sofa_pbrpc_meta.proto

libbase.a:base/third_party/dmg_fp/base_g_fmt.o \
  base/third_party/dmg_fp/base_dtoa_wrapper.o \
  base/third_party/dmg_fp/base_dtoa.o \
  base/third_party/dynamic_annotations/base_dynamic_annotations.o \
  base/third_party/icu/base_icu_utf.o \
  base/third_party/superfasthash/base_superfasthash.o \
  base/third_party/modp_b64/base_modp_b64.o \
  base/third_party/nspr/base_prtime.o \
  base/third_party/symbolize/base_demangle.o \
  base/third_party/symbolize/base_symbolize.o \
  base/third_party/xdg_mime/base_xdgmime.o \
  base/third_party/xdg_mime/base_xdgmimealias.o \
  base/third_party/xdg_mime/base_xdgmimecache.o \
  base/third_party/xdg_mime/base_xdgmimeglob.o \
  base/third_party/xdg_mime/base_xdgmimeicon.o \
  base/third_party/xdg_mime/base_xdgmimeint.o \
  base/third_party/xdg_mime/base_xdgmimemagic.o \
  base/third_party/xdg_mime/base_xdgmimeparent.o \
  base/third_party/xdg_user_dirs/base_xdg_user_dir_lookup.o \
  base/third_party/snappy/base_snappy-sinksource.o \
  base/third_party/snappy/base_snappy-stubs-internal.o \
  base/third_party/snappy/base_snappy.o \
  base/third_party/murmurhash3/base_murmurhash3.o \
  base/allocator/base_type_profiler_control.o \
  base/base_arena.o \
  base/base_at_exit.o \
  base/base_atomicops_internals_x86_gcc.o \
  base/base_barrier_closure.o \
  base/base_base_paths.o \
  base/base_base_paths_posix.o \
  base/base_base64.o \
  base/base_base_switches.o \
  base/base_big_endian.o \
  base/base_bind_helpers.o \
  base/base_build_time.o \
  base/base_callback_helpers.o \
  base/base_callback_internal.o \
  base/base_command_line.o \
  base/base_cpu.o \
  base/debug/base_alias.o \
  base/debug/base_asan_invalid_access.o \
  base/debug/base_crash_logging.o \
  base/debug/base_debugger.o \
  base/debug/base_debugger_posix.o \
  base/debug/base_dump_without_crashing.o \
  base/debug/base_proc_maps_linux.o \
  base/debug/base_stack_trace.o \
  base/debug/base_stack_trace_posix.o \
  base/base_environment.o \
  base/files/base_file.o \
  base/files/base_file_posix.o \
  base/files/base_file_enumerator.o \
  base/files/base_file_enumerator_posix.o \
  base/files/base_file_path.o \
  base/files/base_file_path_constants.o \
  base/files/base_memory_mapped_file.o \
  base/files/base_memory_mapped_file_posix.o \
  base/files/base_scoped_file.o \
  base/files/base_scoped_temp_dir.o \
  base/base_file_util.o \
  base/base_file_util_linux.o \
  base/base_file_util_posix.o \
  base/base_guid.o \
  base/base_guid_posix.o \
  base/base_hash.o \
  base/base_lazy_instance.o \
  base/base_location.o \
  base/base_md5.o \
  base/memory/base_aligned_memory.o \
  base/memory/base_ref_counted.o \
  base/memory/base_ref_counted_memory.o \
  base/memory/base_shared_memory_posix.o \
  base/memory/base_singleton.o \
  base/memory/base_weak_ptr.o \
  base/nix/base_mime_util_xdg.o \
  base/nix/base_xdg_util.o \
  base/base_path_service.o \
  base/posix/base_file_descriptor_shuffle.o \
  base/posix/base_global_descriptors.o \
  base/process/base_internal_linux.o \
  base/process/base_kill.o \
  base/process/base_kill_posix.o \
  base/process/base_launch.o \
  base/process/base_launch_posix.o \
  base/process/base_memory.o \
  base/process/base_memory_linux.o \
  base/process/base_process_handle_linux.o \
  base/process/base_process_handle_posix.o \
  base/process/base_process_info_linux.o \
  base/process/base_process_iterator.o \
  base/process/base_process_iterator_linux.o \
  base/process/base_process_linux.o \
  base/process/base_process_metrics.o \
  base/process/base_process_metrics_linux.o \
  base/process/base_process_metrics_posix.o \
  base/process/base_process_posix.o \
  base/base_rand_util.o \
  base/base_rand_util_posix.o \
  base/base_fast_rand.o \
  base/base_safe_strerror_posix.o \
  base/base_sha1_portable.o \
  base/strings/base_latin1_string_conversions.o \
  base/strings/base_nullable_string16.o \
  base/strings/base_safe_sprintf.o \
  base/strings/base_string16.o \
  base/strings/base_string_number_conversions.o \
  base/strings/base_string_split.o \
  base/strings/base_string_piece.o \
  base/strings/base_string_util.o \
  base/strings/base_string_util_constants.o \
  base/strings/base_stringprintf.o \
  base/strings/base_sys_string_conversions_posix.o \
  base/strings/base_utf_offset_string_conversions.o \
  base/strings/base_utf_string_conversion_utils.o \
  base/strings/base_utf_string_conversions.o \
  base/synchronization/base_cancellation_flag.o \
  base/synchronization/base_condition_variable_posix.o \
  base/synchronization/base_waitable_event_posix.o \
  base/base_sys_info.o \
  base/base_sys_info_linux.o \
  base/base_sys_info_posix.o \
  base/threading/base_non_thread_safe_impl.o \
  base/threading/base_platform_thread_linux.o \
  base/threading/base_platform_thread_posix.o \
  base/threading/base_simple_thread.o \
  base/threading/base_thread_checker_impl.o \
  base/threading/base_thread_collision_warner.o \
  base/threading/base_thread_id_name_manager.o \
  base/threading/base_thread_local_posix.o \
  base/threading/base_thread_local_storage.o \
  base/threading/base_thread_local_storage_posix.o \
  base/threading/base_thread_restrictions.o \
  base/threading/base_watchdog.o \
  base/time/base_clock.o \
  base/time/base_default_clock.o \
  base/time/base_default_tick_clock.o \
  base/time/base_tick_clock.o \
  base/time/base_time.o \
  base/time/base_time_posix.o \
  base/base_version.o \
  base/base_logging.o \
  base/base_class_name.o \
  base/base_errno.o \
  base/base_find_cstr.o \
  base/base_status.o \
  base/base_string_printf.o \
  base/base_thread_local.o \
  base/base_unix_socket.o \
  base/base_endpoint.o \
  base/base_fd_utility.o \
  base/files/base_temp_file.o \
  base/files/base_file_watcher.o \
  base/base_time.o \
  base/base_zero_copy_stream_as_streambuf.o \
  base/base_crc32c.o \
  base/containers/base_case_ignored_flat_map.o \
  base/base_iobuf.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlibbase.a[0m']"
	ar crs libbase.a base/third_party/dmg_fp/base_g_fmt.o \
  base/third_party/dmg_fp/base_dtoa_wrapper.o \
  base/third_party/dmg_fp/base_dtoa.o \
  base/third_party/dynamic_annotations/base_dynamic_annotations.o \
  base/third_party/icu/base_icu_utf.o \
  base/third_party/superfasthash/base_superfasthash.o \
  base/third_party/modp_b64/base_modp_b64.o \
  base/third_party/nspr/base_prtime.o \
  base/third_party/symbolize/base_demangle.o \
  base/third_party/symbolize/base_symbolize.o \
  base/third_party/xdg_mime/base_xdgmime.o \
  base/third_party/xdg_mime/base_xdgmimealias.o \
  base/third_party/xdg_mime/base_xdgmimecache.o \
  base/third_party/xdg_mime/base_xdgmimeglob.o \
  base/third_party/xdg_mime/base_xdgmimeicon.o \
  base/third_party/xdg_mime/base_xdgmimeint.o \
  base/third_party/xdg_mime/base_xdgmimemagic.o \
  base/third_party/xdg_mime/base_xdgmimeparent.o \
  base/third_party/xdg_user_dirs/base_xdg_user_dir_lookup.o \
  base/third_party/snappy/base_snappy-sinksource.o \
  base/third_party/snappy/base_snappy-stubs-internal.o \
  base/third_party/snappy/base_snappy.o \
  base/third_party/murmurhash3/base_murmurhash3.o \
  base/allocator/base_type_profiler_control.o \
  base/base_arena.o \
  base/base_at_exit.o \
  base/base_atomicops_internals_x86_gcc.o \
  base/base_barrier_closure.o \
  base/base_base_paths.o \
  base/base_base_paths_posix.o \
  base/base_base64.o \
  base/base_base_switches.o \
  base/base_big_endian.o \
  base/base_bind_helpers.o \
  base/base_build_time.o \
  base/base_callback_helpers.o \
  base/base_callback_internal.o \
  base/base_command_line.o \
  base/base_cpu.o \
  base/debug/base_alias.o \
  base/debug/base_asan_invalid_access.o \
  base/debug/base_crash_logging.o \
  base/debug/base_debugger.o \
  base/debug/base_debugger_posix.o \
  base/debug/base_dump_without_crashing.o \
  base/debug/base_proc_maps_linux.o \
  base/debug/base_stack_trace.o \
  base/debug/base_stack_trace_posix.o \
  base/base_environment.o \
  base/files/base_file.o \
  base/files/base_file_posix.o \
  base/files/base_file_enumerator.o \
  base/files/base_file_enumerator_posix.o \
  base/files/base_file_path.o \
  base/files/base_file_path_constants.o \
  base/files/base_memory_mapped_file.o \
  base/files/base_memory_mapped_file_posix.o \
  base/files/base_scoped_file.o \
  base/files/base_scoped_temp_dir.o \
  base/base_file_util.o \
  base/base_file_util_linux.o \
  base/base_file_util_posix.o \
  base/base_guid.o \
  base/base_guid_posix.o \
  base/base_hash.o \
  base/base_lazy_instance.o \
  base/base_location.o \
  base/base_md5.o \
  base/memory/base_aligned_memory.o \
  base/memory/base_ref_counted.o \
  base/memory/base_ref_counted_memory.o \
  base/memory/base_shared_memory_posix.o \
  base/memory/base_singleton.o \
  base/memory/base_weak_ptr.o \
  base/nix/base_mime_util_xdg.o \
  base/nix/base_xdg_util.o \
  base/base_path_service.o \
  base/posix/base_file_descriptor_shuffle.o \
  base/posix/base_global_descriptors.o \
  base/process/base_internal_linux.o \
  base/process/base_kill.o \
  base/process/base_kill_posix.o \
  base/process/base_launch.o \
  base/process/base_launch_posix.o \
  base/process/base_memory.o \
  base/process/base_memory_linux.o \
  base/process/base_process_handle_linux.o \
  base/process/base_process_handle_posix.o \
  base/process/base_process_info_linux.o \
  base/process/base_process_iterator.o \
  base/process/base_process_iterator_linux.o \
  base/process/base_process_linux.o \
  base/process/base_process_metrics.o \
  base/process/base_process_metrics_linux.o \
  base/process/base_process_metrics_posix.o \
  base/process/base_process_posix.o \
  base/base_rand_util.o \
  base/base_rand_util_posix.o \
  base/base_fast_rand.o \
  base/base_safe_strerror_posix.o \
  base/base_sha1_portable.o \
  base/strings/base_latin1_string_conversions.o \
  base/strings/base_nullable_string16.o \
  base/strings/base_safe_sprintf.o \
  base/strings/base_string16.o \
  base/strings/base_string_number_conversions.o \
  base/strings/base_string_split.o \
  base/strings/base_string_piece.o \
  base/strings/base_string_util.o \
  base/strings/base_string_util_constants.o \
  base/strings/base_stringprintf.o \
  base/strings/base_sys_string_conversions_posix.o \
  base/strings/base_utf_offset_string_conversions.o \
  base/strings/base_utf_string_conversion_utils.o \
  base/strings/base_utf_string_conversions.o \
  base/synchronization/base_cancellation_flag.o \
  base/synchronization/base_condition_variable_posix.o \
  base/synchronization/base_waitable_event_posix.o \
  base/base_sys_info.o \
  base/base_sys_info_linux.o \
  base/base_sys_info_posix.o \
  base/threading/base_non_thread_safe_impl.o \
  base/threading/base_platform_thread_linux.o \
  base/threading/base_platform_thread_posix.o \
  base/threading/base_simple_thread.o \
  base/threading/base_thread_checker_impl.o \
  base/threading/base_thread_collision_warner.o \
  base/threading/base_thread_id_name_manager.o \
  base/threading/base_thread_local_posix.o \
  base/threading/base_thread_local_storage.o \
  base/threading/base_thread_local_storage_posix.o \
  base/threading/base_thread_restrictions.o \
  base/threading/base_watchdog.o \
  base/time/base_clock.o \
  base/time/base_default_clock.o \
  base/time/base_default_tick_clock.o \
  base/time/base_tick_clock.o \
  base/time/base_time.o \
  base/time/base_time_posix.o \
  base/base_version.o \
  base/base_logging.o \
  base/base_class_name.o \
  base/base_errno.o \
  base/base_find_cstr.o \
  base/base_status.o \
  base/base_string_printf.o \
  base/base_thread_local.o \
  base/base_unix_socket.o \
  base/base_endpoint.o \
  base/base_fd_utility.o \
  base/files/base_temp_file.o \
  base/files/base_file_watcher.o \
  base/base_time.o \
  base/base_zero_copy_stream_as_streambuf.o \
  base/base_crc32c.o \
  base/containers/base_case_ignored_flat_map.o \
  base/base_iobuf.o
	mkdir -p ./output/lib
	cp -f libbase.a ./output/lib

libbvar.a:bvar/bvar_collector.o \
  bvar/bvar_default_variables.o \
  bvar/bvar_gflag.o \
  bvar/bvar_latency_recorder.o \
  bvar/bvar_variable.o \
  bvar/detail/bvar_percentile.o \
  bvar/detail/bvar_sampler.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlibbvar.a[0m']"
	ar crs libbvar.a bvar/bvar_collector.o \
  bvar/bvar_default_variables.o \
  bvar/bvar_gflag.o \
  bvar/bvar_latency_recorder.o \
  bvar/bvar_variable.o \
  bvar/detail/bvar_percentile.o \
  bvar/detail/bvar_sampler.o
	mkdir -p ./output/lib
	cp -f libbvar.a ./output/lib

libbthread.a:bthread/bthread_bthread.o \
  bthread/bthread_butex.o \
  bthread/bthread_cond.o \
  bthread/bthread_context.o \
  bthread/bthread_countdown_event.o \
  bthread/bthread_errno.o \
  bthread/bthread_execution_queue.o \
  bthread/bthread_fd.o \
  bthread/bthread_id.o \
  bthread/bthread_interrupt_pthread.o \
  bthread/bthread_key.o \
  bthread/bthread_mutex.o \
  bthread/bthread_stack.o \
  bthread/bthread_sys_futex.o \
  bthread/bthread_task_control.o \
  bthread/bthread_task_group.o \
  bthread/bthread_timer_thread.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlibbthread.a[0m']"
	ar crs libbthread.a bthread/bthread_bthread.o \
  bthread/bthread_butex.o \
  bthread/bthread_cond.o \
  bthread/bthread_context.o \
  bthread/bthread_countdown_event.o \
  bthread/bthread_errno.o \
  bthread/bthread_execution_queue.o \
  bthread/bthread_fd.o \
  bthread/bthread_id.o \
  bthread/bthread_interrupt_pthread.o \
  bthread/bthread_key.o \
  bthread/bthread_mutex.o \
  bthread/bthread_stack.o \
  bthread/bthread_sys_futex.o \
  bthread/bthread_task_control.o \
  bthread/bthread_task_group.o \
  bthread/bthread_timer_thread.o
	mkdir -p ./output/lib
	cp -f libbthread.a ./output/lib

libjson2pb.a:json2pb/json2pb_encode_decode.o \
  json2pb/json2pb_json_to_pb.o \
  json2pb/json2pb_pb_to_json.o \
  json2pb/json2pb_protobuf_map.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlibjson2pb.a[0m']"
	ar crs libjson2pb.a json2pb/json2pb_encode_decode.o \
  json2pb/json2pb_json_to_pb.o \
  json2pb/json2pb_pb_to_json.o \
  json2pb/json2pb_protobuf_map.o
	mkdir -p ./output/lib
	cp -f libjson2pb.a ./output/lib

libmcpack2pb.a:mcpack2pb/mcpack2pb_field_type.o \
  mcpack2pb/mcpack2pb_mcpack2pb.o \
  mcpack2pb/mcpack2pb_parser.o \
  mcpack2pb/mcpack2pb_serializer.o \
  mcpack2pb_idl_options.pb.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlibmcpack2pb.a[0m']"
	ar crs libmcpack2pb.a mcpack2pb/mcpack2pb_field_type.o \
  mcpack2pb/mcpack2pb_mcpack2pb.o \
  mcpack2pb/mcpack2pb_parser.o \
  mcpack2pb/mcpack2pb_serializer.o \
  mcpack2pb_idl_options.pb.o
	mkdir -p ./output/lib
	cp -f libmcpack2pb.a ./output/lib

protoc-gen-mcpack:mcpack2pb/protoc-gen-mcpack_generator.o \
  libmcpack2pb.a \
  libbase.a \
  libbthread.a \
  libbvar.a
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mprotoc-gen-mcpack[0m']"
	$(CXX) mcpack2pb/protoc-gen-mcpack_generator.o -Xlinker "-(" libmcpack2pb.a \
  libbase.a \
  libbthread.a \
  libbvar.a ../../../third-64/gflags/lib/libgflags.a \
  ../../../third-64/gflags/lib/libgflags_nothreads.a \
  ../../../third-64/leveldb/lib/libleveldb.a \
  ../../../third-64/protobuf/lib/libprotobuf-lite.a \
  ../../../third-64/protobuf/lib/libprotobuf.a \
  ../../../third-64/protobuf/lib/libprotoc.a -lpthread \
  -lrt \
  -lssl \
  -lcrypto \
  -ldl \
  -lz -Xlinker "-)" -o protoc-gen-mcpack
	mkdir -p ./output/bin
	cp -f protoc-gen-mcpack ./output/bin

libbrpc.a:brpc/brpc_acceptor.o \
  brpc/brpc_adaptive_connection_type.o \
  brpc/brpc_amf.o \
  brpc/brpc_bad_method_service.o \
  brpc/brpc_channel.o \
  brpc/brpc_compress.o \
  brpc/brpc_controller.o \
  brpc/brpc_esp_message.o \
  brpc/brpc_event_dispatcher.o \
  brpc/brpc_global.o \
  brpc/brpc_http_header.o \
  brpc/brpc_http_method.o \
  brpc/brpc_http_status_code.o \
  brpc/brpc_input_messenger.o \
  brpc/brpc_load_balancer.o \
  brpc/brpc_load_balancer_with_naming.o \
  brpc/brpc_memcache.o \
  brpc/brpc_naming_service_thread.o \
  brpc/brpc_nshead_message.o \
  brpc/brpc_nshead_pb_service_adaptor.o \
  brpc/brpc_nshead_service.o \
  brpc/brpc_parallel_channel.o \
  brpc/brpc_partition_channel.o \
  brpc/brpc_periodic_naming_service.o \
  brpc/brpc_progressive_attachment.o \
  brpc/brpc_protocol.o \
  brpc/brpc_redis.o \
  brpc/brpc_redis_command.o \
  brpc/brpc_redis_reply.o \
  brpc/brpc_reloadable_flags.o \
  brpc/brpc_restful.o \
  brpc/brpc_retry_policy.o \
  brpc/brpc_rpc_dump.o \
  brpc/brpc_rtmp.o \
  brpc/brpc_selective_channel.o \
  brpc/brpc_serialized_request.o \
  brpc/brpc_server.o \
  brpc/brpc_server_id.o \
  brpc/brpc_socket.o \
  brpc/brpc_socket_map.o \
  brpc/brpc_span.o \
  brpc/brpc_stream.o \
  brpc/brpc_tcmalloc_extension.o \
  brpc/brpc_trackme.o \
  brpc/brpc_ts.o \
  brpc/brpc_uri.o \
  brpc/policy/brpc_baidu_rpc_protocol.o \
  brpc/policy/brpc_consistent_hashing_load_balancer.o \
  brpc/policy/brpc_dh.o \
  brpc/policy/brpc_domain_naming_service.o \
  brpc/policy/brpc_dynpart_load_balancer.o \
  brpc/policy/brpc_esp_authenticator.o \
  brpc/policy/brpc_esp_protocol.o \
  brpc/policy/brpc_file_naming_service.o \
  brpc/policy/brpc_gzip_compress.o \
  brpc/policy/brpc_hasher.o \
  brpc/policy/brpc_http_rpc_protocol.o \
  brpc/policy/brpc_hulu_pbrpc_protocol.o \
  brpc/policy/brpc_list_naming_service.o \
  brpc/policy/brpc_locality_aware_load_balancer.o \
  brpc/policy/brpc_memcache_binary_protocol.o \
  brpc/policy/brpc_mongo_protocol.o \
  brpc/policy/brpc_nova_pbrpc_protocol.o \
  brpc/policy/brpc_nshead_mcpack_protocol.o \
  brpc/policy/brpc_nshead_protocol.o \
  brpc/policy/brpc_public_pbrpc_protocol.o \
  brpc/policy/brpc_randomized_load_balancer.o \
  brpc/policy/brpc_redis_protocol.o \
  brpc/policy/brpc_remote_file_naming_service.o \
  brpc/policy/brpc_round_robin_load_balancer.o \
  brpc/policy/brpc_rtmp_protocol.o \
  brpc/policy/brpc_snappy_compress.o \
  brpc/policy/brpc_sofa_pbrpc_protocol.o \
  brpc/policy/brpc_streaming_rpc_protocol.o \
  brpc/policy/brpc_ubrpc2pb_protocol.o \
  brpc/builtin/brpc_bthreads_service.o \
  brpc/builtin/brpc_common.o \
  brpc/builtin/brpc_connections_service.o \
  brpc/builtin/brpc_dir_service.o \
  brpc/builtin/brpc_flags_service.o \
  brpc/builtin/brpc_flot_min_js.o \
  brpc/builtin/brpc_get_favicon_service.o \
  brpc/builtin/brpc_get_js_service.o \
  brpc/builtin/brpc_health_service.o \
  brpc/builtin/brpc_hotspots_service.o \
  brpc/builtin/brpc_ids_service.o \
  brpc/builtin/brpc_index_service.o \
  brpc/builtin/brpc_jquery_min_js.o \
  brpc/builtin/brpc_list_service.o \
  brpc/builtin/brpc_pprof_perl.o \
  brpc/builtin/brpc_pprof_service.o \
  brpc/builtin/brpc_protobufs_service.o \
  brpc/builtin/brpc_rpcz_service.o \
  brpc/builtin/brpc_sockets_service.o \
  brpc/builtin/brpc_sorttable_js.o \
  brpc/builtin/brpc_status_service.o \
  brpc/builtin/brpc_threads_service.o \
  brpc/builtin/brpc_vars_service.o \
  brpc/builtin/brpc_version_service.o \
  brpc/builtin/brpc_viz_min_js.o \
  brpc/builtin/brpc_vlog_service.o \
  brpc/details/brpc_has_epollrdhup.o \
  brpc/details/brpc_hpack.o \
  brpc/details/brpc_http_message.o \
  brpc/details/brpc_http_message_serializer.o \
  brpc/details/brpc_http_parser.o \
  brpc/details/brpc_method_status.o \
  brpc/details/brpc_rtmp_utils.o \
  brpc/details/brpc_ssl_helper.o \
  brpc/details/brpc_usercode_backup_pool.o \
  brpc/brpc_builtin_service.pb.o \
  brpc/brpc_errno.pb.o \
  brpc/brpc_get_favicon.pb.o \
  brpc/brpc_get_js.pb.o \
  brpc/brpc_nshead_meta.pb.o \
  brpc/brpc_options.pb.o \
  brpc/brpc_rpc_dump.pb.o \
  brpc/brpc_rtmp.pb.o \
  brpc/brpc_span.pb.o \
  brpc/brpc_streaming_rpc_meta.pb.o \
  brpc/brpc_trackme.pb.o \
  brpc/policy/brpc_baidu_rpc_meta.pb.o \
  brpc/policy/brpc_hulu_pbrpc_meta.pb.o \
  brpc/policy/brpc_mongo.pb.o \
  brpc/policy/brpc_public_pbrpc_meta.pb.o \
  brpc/policy/brpc_sofa_pbrpc_meta.pb.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlibbrpc.a[0m']"
	ar crs libbrpc.a brpc/brpc_acceptor.o \
  brpc/brpc_adaptive_connection_type.o \
  brpc/brpc_amf.o \
  brpc/brpc_bad_method_service.o \
  brpc/brpc_channel.o \
  brpc/brpc_compress.o \
  brpc/brpc_controller.o \
  brpc/brpc_esp_message.o \
  brpc/brpc_event_dispatcher.o \
  brpc/brpc_global.o \
  brpc/brpc_http_header.o \
  brpc/brpc_http_method.o \
  brpc/brpc_http_status_code.o \
  brpc/brpc_input_messenger.o \
  brpc/brpc_load_balancer.o \
  brpc/brpc_load_balancer_with_naming.o \
  brpc/brpc_memcache.o \
  brpc/brpc_naming_service_thread.o \
  brpc/brpc_nshead_message.o \
  brpc/brpc_nshead_pb_service_adaptor.o \
  brpc/brpc_nshead_service.o \
  brpc/brpc_parallel_channel.o \
  brpc/brpc_partition_channel.o \
  brpc/brpc_periodic_naming_service.o \
  brpc/brpc_progressive_attachment.o \
  brpc/brpc_protocol.o \
  brpc/brpc_redis.o \
  brpc/brpc_redis_command.o \
  brpc/brpc_redis_reply.o \
  brpc/brpc_reloadable_flags.o \
  brpc/brpc_restful.o \
  brpc/brpc_retry_policy.o \
  brpc/brpc_rpc_dump.o \
  brpc/brpc_rtmp.o \
  brpc/brpc_selective_channel.o \
  brpc/brpc_serialized_request.o \
  brpc/brpc_server.o \
  brpc/brpc_server_id.o \
  brpc/brpc_socket.o \
  brpc/brpc_socket_map.o \
  brpc/brpc_span.o \
  brpc/brpc_stream.o \
  brpc/brpc_tcmalloc_extension.o \
  brpc/brpc_trackme.o \
  brpc/brpc_ts.o \
  brpc/brpc_uri.o \
  brpc/policy/brpc_baidu_rpc_protocol.o \
  brpc/policy/brpc_consistent_hashing_load_balancer.o \
  brpc/policy/brpc_dh.o \
  brpc/policy/brpc_domain_naming_service.o \
  brpc/policy/brpc_dynpart_load_balancer.o \
  brpc/policy/brpc_esp_authenticator.o \
  brpc/policy/brpc_esp_protocol.o \
  brpc/policy/brpc_file_naming_service.o \
  brpc/policy/brpc_gzip_compress.o \
  brpc/policy/brpc_hasher.o \
  brpc/policy/brpc_http_rpc_protocol.o \
  brpc/policy/brpc_hulu_pbrpc_protocol.o \
  brpc/policy/brpc_list_naming_service.o \
  brpc/policy/brpc_locality_aware_load_balancer.o \
  brpc/policy/brpc_memcache_binary_protocol.o \
  brpc/policy/brpc_mongo_protocol.o \
  brpc/policy/brpc_nova_pbrpc_protocol.o \
  brpc/policy/brpc_nshead_mcpack_protocol.o \
  brpc/policy/brpc_nshead_protocol.o \
  brpc/policy/brpc_public_pbrpc_protocol.o \
  brpc/policy/brpc_randomized_load_balancer.o \
  brpc/policy/brpc_redis_protocol.o \
  brpc/policy/brpc_remote_file_naming_service.o \
  brpc/policy/brpc_round_robin_load_balancer.o \
  brpc/policy/brpc_rtmp_protocol.o \
  brpc/policy/brpc_snappy_compress.o \
  brpc/policy/brpc_sofa_pbrpc_protocol.o \
  brpc/policy/brpc_streaming_rpc_protocol.o \
  brpc/policy/brpc_ubrpc2pb_protocol.o \
  brpc/builtin/brpc_bthreads_service.o \
  brpc/builtin/brpc_common.o \
  brpc/builtin/brpc_connections_service.o \
  brpc/builtin/brpc_dir_service.o \
  brpc/builtin/brpc_flags_service.o \
  brpc/builtin/brpc_flot_min_js.o \
  brpc/builtin/brpc_get_favicon_service.o \
  brpc/builtin/brpc_get_js_service.o \
  brpc/builtin/brpc_health_service.o \
  brpc/builtin/brpc_hotspots_service.o \
  brpc/builtin/brpc_ids_service.o \
  brpc/builtin/brpc_index_service.o \
  brpc/builtin/brpc_jquery_min_js.o \
  brpc/builtin/brpc_list_service.o \
  brpc/builtin/brpc_pprof_perl.o \
  brpc/builtin/brpc_pprof_service.o \
  brpc/builtin/brpc_protobufs_service.o \
  brpc/builtin/brpc_rpcz_service.o \
  brpc/builtin/brpc_sockets_service.o \
  brpc/builtin/brpc_sorttable_js.o \
  brpc/builtin/brpc_status_service.o \
  brpc/builtin/brpc_threads_service.o \
  brpc/builtin/brpc_vars_service.o \
  brpc/builtin/brpc_version_service.o \
  brpc/builtin/brpc_viz_min_js.o \
  brpc/builtin/brpc_vlog_service.o \
  brpc/details/brpc_has_epollrdhup.o \
  brpc/details/brpc_hpack.o \
  brpc/details/brpc_http_message.o \
  brpc/details/brpc_http_message_serializer.o \
  brpc/details/brpc_http_parser.o \
  brpc/details/brpc_method_status.o \
  brpc/details/brpc_rtmp_utils.o \
  brpc/details/brpc_ssl_helper.o \
  brpc/details/brpc_usercode_backup_pool.o \
  brpc/brpc_builtin_service.pb.o \
  brpc/brpc_errno.pb.o \
  brpc/brpc_get_favicon.pb.o \
  brpc/brpc_get_js.pb.o \
  brpc/brpc_nshead_meta.pb.o \
  brpc/brpc_options.pb.o \
  brpc/brpc_rpc_dump.pb.o \
  brpc/brpc_rtmp.pb.o \
  brpc/brpc_span.pb.o \
  brpc/brpc_streaming_rpc_meta.pb.o \
  brpc/brpc_trackme.pb.o \
  brpc/policy/brpc_baidu_rpc_meta.pb.o \
  brpc/policy/brpc_hulu_pbrpc_meta.pb.o \
  brpc/policy/brpc_mongo.pb.o \
  brpc/policy/brpc_public_pbrpc_meta.pb.o \
  brpc/policy/brpc_sofa_pbrpc_meta.pb.o
	mkdir -p ./output/lib
	cp -f libbrpc.a ./output/lib

.PHONY:output/include
output/include:libbase.a \
  libbvar.a \
  libbthread.a \
  libjson2pb.a \
  libmcpack2pb.a \
  libbrpc.a
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40moutput/include[0m']"
	for dir in `find base bvar bthread brpc -type f -name "*.h" -exec dirname {} \; | sort | uniq`; do mkdir -p output/include/$$dir && cp $$dir/*.h output/include/$$dir/; done; for dir in `find base bvar bthread brpc -type f -name "*.hpp" -exec dirname {} \; | sort | uniq`; do mkdir -p output/include/$$dir && cp $$dir/*.hpp output/include/$$dir/; done

base/third_party/dmg_fp/base_g_fmt.o:base/third_party/dmg_fp/g_fmt.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/dmg_fp/base_g_fmt.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/dmg_fp/base_g_fmt.o base/third_party/dmg_fp/g_fmt.cc

base/third_party/dmg_fp/base_dtoa_wrapper.o:base/third_party/dmg_fp/dtoa_wrapper.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/dmg_fp/base_dtoa_wrapper.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/dmg_fp/base_dtoa_wrapper.o base/third_party/dmg_fp/dtoa_wrapper.cc

base/third_party/dmg_fp/base_dtoa.o:base/third_party/dmg_fp/dtoa.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/dmg_fp/base_dtoa.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/dmg_fp/base_dtoa.o base/third_party/dmg_fp/dtoa.cc

base/third_party/dynamic_annotations/base_dynamic_annotations.o:base/third_party/dynamic_annotations/dynamic_annotations.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/dynamic_annotations/base_dynamic_annotations.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/dynamic_annotations/base_dynamic_annotations.o base/third_party/dynamic_annotations/dynamic_annotations.c

base/third_party/icu/base_icu_utf.o:base/third_party/icu/icu_utf.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/icu/base_icu_utf.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/icu/base_icu_utf.o base/third_party/icu/icu_utf.cc

base/third_party/superfasthash/base_superfasthash.o:base/third_party/superfasthash/superfasthash.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/superfasthash/base_superfasthash.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/superfasthash/base_superfasthash.o base/third_party/superfasthash/superfasthash.c

base/third_party/modp_b64/base_modp_b64.o:base/third_party/modp_b64/modp_b64.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/modp_b64/base_modp_b64.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/modp_b64/base_modp_b64.o base/third_party/modp_b64/modp_b64.cc

base/third_party/nspr/base_prtime.o:base/third_party/nspr/prtime.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/nspr/base_prtime.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/nspr/base_prtime.o base/third_party/nspr/prtime.cc

base/third_party/symbolize/base_demangle.o:base/third_party/symbolize/demangle.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/symbolize/base_demangle.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/symbolize/base_demangle.o base/third_party/symbolize/demangle.cc

base/third_party/symbolize/base_symbolize.o:base/third_party/symbolize/symbolize.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/symbolize/base_symbolize.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/symbolize/base_symbolize.o base/third_party/symbolize/symbolize.cc

base/third_party/xdg_mime/base_xdgmime.o:base/third_party/xdg_mime/xdgmime.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmime.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmime.o base/third_party/xdg_mime/xdgmime.c

base/third_party/xdg_mime/base_xdgmimealias.o:base/third_party/xdg_mime/xdgmimealias.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmimealias.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmimealias.o base/third_party/xdg_mime/xdgmimealias.c

base/third_party/xdg_mime/base_xdgmimecache.o:base/third_party/xdg_mime/xdgmimecache.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmimecache.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmimecache.o base/third_party/xdg_mime/xdgmimecache.c

base/third_party/xdg_mime/base_xdgmimeglob.o:base/third_party/xdg_mime/xdgmimeglob.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmimeglob.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmimeglob.o base/third_party/xdg_mime/xdgmimeglob.c

base/third_party/xdg_mime/base_xdgmimeicon.o:base/third_party/xdg_mime/xdgmimeicon.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmimeicon.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmimeicon.o base/third_party/xdg_mime/xdgmimeicon.c

base/third_party/xdg_mime/base_xdgmimeint.o:base/third_party/xdg_mime/xdgmimeint.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmimeint.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmimeint.o base/third_party/xdg_mime/xdgmimeint.c

base/third_party/xdg_mime/base_xdgmimemagic.o:base/third_party/xdg_mime/xdgmimemagic.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmimemagic.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmimemagic.o base/third_party/xdg_mime/xdgmimemagic.c

base/third_party/xdg_mime/base_xdgmimeparent.o:base/third_party/xdg_mime/xdgmimeparent.c
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_mime/base_xdgmimeparent.o[0m']"
	$(CC) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CFLAGS)  -o base/third_party/xdg_mime/base_xdgmimeparent.o base/third_party/xdg_mime/xdgmimeparent.c

base/third_party/xdg_user_dirs/base_xdg_user_dir_lookup.o:base/third_party/xdg_user_dirs/xdg_user_dir_lookup.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/xdg_user_dirs/base_xdg_user_dir_lookup.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/xdg_user_dirs/base_xdg_user_dir_lookup.o base/third_party/xdg_user_dirs/xdg_user_dir_lookup.cc

base/third_party/snappy/base_snappy-sinksource.o:base/third_party/snappy/snappy-sinksource.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/snappy/base_snappy-sinksource.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/snappy/base_snappy-sinksource.o base/third_party/snappy/snappy-sinksource.cc

base/third_party/snappy/base_snappy-stubs-internal.o:base/third_party/snappy/snappy-stubs-internal.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/snappy/base_snappy-stubs-internal.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/snappy/base_snappy-stubs-internal.o base/third_party/snappy/snappy-stubs-internal.cc

base/third_party/snappy/base_snappy.o:base/third_party/snappy/snappy.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/snappy/base_snappy.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/snappy/base_snappy.o base/third_party/snappy/snappy.cc

base/third_party/murmurhash3/base_murmurhash3.o:base/third_party/murmurhash3/murmurhash3.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/third_party/murmurhash3/base_murmurhash3.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/third_party/murmurhash3/base_murmurhash3.o base/third_party/murmurhash3/murmurhash3.cpp

base/allocator/base_type_profiler_control.o:base/allocator/type_profiler_control.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/allocator/base_type_profiler_control.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/allocator/base_type_profiler_control.o base/allocator/type_profiler_control.cc

base/base_arena.o:base/arena.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_arena.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_arena.o base/arena.cpp

base/base_at_exit.o:base/at_exit.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_at_exit.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_at_exit.o base/at_exit.cc

base/base_atomicops_internals_x86_gcc.o:base/atomicops_internals_x86_gcc.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_atomicops_internals_x86_gcc.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_atomicops_internals_x86_gcc.o base/atomicops_internals_x86_gcc.cc

base/base_barrier_closure.o:base/barrier_closure.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_barrier_closure.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_barrier_closure.o base/barrier_closure.cc

base/base_base_paths.o:base/base_paths.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_base_paths.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_base_paths.o base/base_paths.cc

base/base_base_paths_posix.o:base/base_paths_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_base_paths_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_base_paths_posix.o base/base_paths_posix.cc

base/base_base64.o:base/base64.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_base64.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_base64.o base/base64.cc

base/base_base_switches.o:base/base_switches.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_base_switches.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_base_switches.o base/base_switches.cc

base/base_big_endian.o:base/big_endian.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_big_endian.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_big_endian.o base/big_endian.cc

base/base_bind_helpers.o:base/bind_helpers.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_bind_helpers.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_bind_helpers.o base/bind_helpers.cc

base/base_build_time.o:base/build_time.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_build_time.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_build_time.o base/build_time.cc

base/base_callback_helpers.o:base/callback_helpers.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_callback_helpers.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_callback_helpers.o base/callback_helpers.cc

base/base_callback_internal.o:base/callback_internal.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_callback_internal.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_callback_internal.o base/callback_internal.cc

base/base_command_line.o:base/command_line.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_command_line.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_command_line.o base/command_line.cc

base/base_cpu.o:base/cpu.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_cpu.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_cpu.o base/cpu.cc

base/debug/base_alias.o:base/debug/alias.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_alias.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_alias.o base/debug/alias.cc

base/debug/base_asan_invalid_access.o:base/debug/asan_invalid_access.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_asan_invalid_access.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_asan_invalid_access.o base/debug/asan_invalid_access.cc

base/debug/base_crash_logging.o:base/debug/crash_logging.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_crash_logging.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_crash_logging.o base/debug/crash_logging.cc

base/debug/base_debugger.o:base/debug/debugger.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_debugger.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_debugger.o base/debug/debugger.cc

base/debug/base_debugger_posix.o:base/debug/debugger_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_debugger_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_debugger_posix.o base/debug/debugger_posix.cc

base/debug/base_dump_without_crashing.o:base/debug/dump_without_crashing.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_dump_without_crashing.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_dump_without_crashing.o base/debug/dump_without_crashing.cc

base/debug/base_proc_maps_linux.o:base/debug/proc_maps_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_proc_maps_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_proc_maps_linux.o base/debug/proc_maps_linux.cc

base/debug/base_stack_trace.o:base/debug/stack_trace.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_stack_trace.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_stack_trace.o base/debug/stack_trace.cc

base/debug/base_stack_trace_posix.o:base/debug/stack_trace_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/debug/base_stack_trace_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/debug/base_stack_trace_posix.o base/debug/stack_trace_posix.cc

base/base_environment.o:base/environment.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_environment.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_environment.o base/environment.cc

base/files/base_file.o:base/files/file.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_file.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_file.o base/files/file.cc

base/files/base_file_posix.o:base/files/file_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_file_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_file_posix.o base/files/file_posix.cc

base/files/base_file_enumerator.o:base/files/file_enumerator.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_file_enumerator.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_file_enumerator.o base/files/file_enumerator.cc

base/files/base_file_enumerator_posix.o:base/files/file_enumerator_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_file_enumerator_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_file_enumerator_posix.o base/files/file_enumerator_posix.cc

base/files/base_file_path.o:base/files/file_path.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_file_path.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_file_path.o base/files/file_path.cc

base/files/base_file_path_constants.o:base/files/file_path_constants.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_file_path_constants.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_file_path_constants.o base/files/file_path_constants.cc

base/files/base_memory_mapped_file.o:base/files/memory_mapped_file.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_memory_mapped_file.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_memory_mapped_file.o base/files/memory_mapped_file.cc

base/files/base_memory_mapped_file_posix.o:base/files/memory_mapped_file_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_memory_mapped_file_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_memory_mapped_file_posix.o base/files/memory_mapped_file_posix.cc

base/files/base_scoped_file.o:base/files/scoped_file.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_scoped_file.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_scoped_file.o base/files/scoped_file.cc

base/files/base_scoped_temp_dir.o:base/files/scoped_temp_dir.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_scoped_temp_dir.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_scoped_temp_dir.o base/files/scoped_temp_dir.cc

base/base_file_util.o:base/file_util.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_file_util.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_file_util.o base/file_util.cc

base/base_file_util_linux.o:base/file_util_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_file_util_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_file_util_linux.o base/file_util_linux.cc

base/base_file_util_posix.o:base/file_util_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_file_util_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_file_util_posix.o base/file_util_posix.cc

base/base_guid.o:base/guid.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_guid.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_guid.o base/guid.cc

base/base_guid_posix.o:base/guid_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_guid_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_guid_posix.o base/guid_posix.cc

base/base_hash.o:base/hash.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_hash.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_hash.o base/hash.cc

base/base_lazy_instance.o:base/lazy_instance.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_lazy_instance.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_lazy_instance.o base/lazy_instance.cc

base/base_location.o:base/location.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_location.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_location.o base/location.cc

base/base_md5.o:base/md5.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_md5.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_md5.o base/md5.cc

base/memory/base_aligned_memory.o:base/memory/aligned_memory.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/memory/base_aligned_memory.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/memory/base_aligned_memory.o base/memory/aligned_memory.cc

base/memory/base_ref_counted.o:base/memory/ref_counted.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/memory/base_ref_counted.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/memory/base_ref_counted.o base/memory/ref_counted.cc

base/memory/base_ref_counted_memory.o:base/memory/ref_counted_memory.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/memory/base_ref_counted_memory.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/memory/base_ref_counted_memory.o base/memory/ref_counted_memory.cc

base/memory/base_shared_memory_posix.o:base/memory/shared_memory_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/memory/base_shared_memory_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/memory/base_shared_memory_posix.o base/memory/shared_memory_posix.cc

base/memory/base_singleton.o:base/memory/singleton.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/memory/base_singleton.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/memory/base_singleton.o base/memory/singleton.cc

base/memory/base_weak_ptr.o:base/memory/weak_ptr.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/memory/base_weak_ptr.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/memory/base_weak_ptr.o base/memory/weak_ptr.cc

base/nix/base_mime_util_xdg.o:base/nix/mime_util_xdg.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/nix/base_mime_util_xdg.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/nix/base_mime_util_xdg.o base/nix/mime_util_xdg.cc

base/nix/base_xdg_util.o:base/nix/xdg_util.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/nix/base_xdg_util.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/nix/base_xdg_util.o base/nix/xdg_util.cc

base/base_path_service.o:base/path_service.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_path_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_path_service.o base/path_service.cc

base/posix/base_file_descriptor_shuffle.o:base/posix/file_descriptor_shuffle.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/posix/base_file_descriptor_shuffle.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/posix/base_file_descriptor_shuffle.o base/posix/file_descriptor_shuffle.cc

base/posix/base_global_descriptors.o:base/posix/global_descriptors.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/posix/base_global_descriptors.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/posix/base_global_descriptors.o base/posix/global_descriptors.cc

base/process/base_internal_linux.o:base/process/internal_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_internal_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_internal_linux.o base/process/internal_linux.cc

base/process/base_kill.o:base/process/kill.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_kill.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_kill.o base/process/kill.cc

base/process/base_kill_posix.o:base/process/kill_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_kill_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_kill_posix.o base/process/kill_posix.cc

base/process/base_launch.o:base/process/launch.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_launch.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_launch.o base/process/launch.cc

base/process/base_launch_posix.o:base/process/launch_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_launch_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_launch_posix.o base/process/launch_posix.cc

base/process/base_memory.o:base/process/memory.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_memory.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_memory.o base/process/memory.cc

base/process/base_memory_linux.o:base/process/memory_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_memory_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_memory_linux.o base/process/memory_linux.cc

base/process/base_process_handle_linux.o:base/process/process_handle_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_handle_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_handle_linux.o base/process/process_handle_linux.cc

base/process/base_process_handle_posix.o:base/process/process_handle_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_handle_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_handle_posix.o base/process/process_handle_posix.cc

base/process/base_process_info_linux.o:base/process/process_info_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_info_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_info_linux.o base/process/process_info_linux.cc

base/process/base_process_iterator.o:base/process/process_iterator.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_iterator.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_iterator.o base/process/process_iterator.cc

base/process/base_process_iterator_linux.o:base/process/process_iterator_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_iterator_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_iterator_linux.o base/process/process_iterator_linux.cc

base/process/base_process_linux.o:base/process/process_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_linux.o base/process/process_linux.cc

base/process/base_process_metrics.o:base/process/process_metrics.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_metrics.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_metrics.o base/process/process_metrics.cc

base/process/base_process_metrics_linux.o:base/process/process_metrics_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_metrics_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_metrics_linux.o base/process/process_metrics_linux.cc

base/process/base_process_metrics_posix.o:base/process/process_metrics_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_metrics_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_metrics_posix.o base/process/process_metrics_posix.cc

base/process/base_process_posix.o:base/process/process_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/process/base_process_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/process/base_process_posix.o base/process/process_posix.cc

base/base_rand_util.o:base/rand_util.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_rand_util.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_rand_util.o base/rand_util.cc

base/base_rand_util_posix.o:base/rand_util_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_rand_util_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_rand_util_posix.o base/rand_util_posix.cc

base/base_fast_rand.o:base/fast_rand.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_fast_rand.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_fast_rand.o base/fast_rand.cpp

base/base_safe_strerror_posix.o:base/safe_strerror_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_safe_strerror_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_safe_strerror_posix.o base/safe_strerror_posix.cc

base/base_sha1_portable.o:base/sha1_portable.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_sha1_portable.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_sha1_portable.o base/sha1_portable.cc

base/strings/base_latin1_string_conversions.o:base/strings/latin1_string_conversions.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_latin1_string_conversions.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_latin1_string_conversions.o base/strings/latin1_string_conversions.cc

base/strings/base_nullable_string16.o:base/strings/nullable_string16.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_nullable_string16.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_nullable_string16.o base/strings/nullable_string16.cc

base/strings/base_safe_sprintf.o:base/strings/safe_sprintf.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_safe_sprintf.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_safe_sprintf.o base/strings/safe_sprintf.cc

base/strings/base_string16.o:base/strings/string16.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_string16.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_string16.o base/strings/string16.cc

base/strings/base_string_number_conversions.o:base/strings/string_number_conversions.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_string_number_conversions.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_string_number_conversions.o base/strings/string_number_conversions.cc

base/strings/base_string_split.o:base/strings/string_split.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_string_split.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_string_split.o base/strings/string_split.cc

base/strings/base_string_piece.o:base/strings/string_piece.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_string_piece.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_string_piece.o base/strings/string_piece.cc

base/strings/base_string_util.o:base/strings/string_util.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_string_util.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_string_util.o base/strings/string_util.cc

base/strings/base_string_util_constants.o:base/strings/string_util_constants.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_string_util_constants.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_string_util_constants.o base/strings/string_util_constants.cc

base/strings/base_stringprintf.o:base/strings/stringprintf.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_stringprintf.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_stringprintf.o base/strings/stringprintf.cc

base/strings/base_sys_string_conversions_posix.o:base/strings/sys_string_conversions_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_sys_string_conversions_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_sys_string_conversions_posix.o base/strings/sys_string_conversions_posix.cc

base/strings/base_utf_offset_string_conversions.o:base/strings/utf_offset_string_conversions.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_utf_offset_string_conversions.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_utf_offset_string_conversions.o base/strings/utf_offset_string_conversions.cc

base/strings/base_utf_string_conversion_utils.o:base/strings/utf_string_conversion_utils.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_utf_string_conversion_utils.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_utf_string_conversion_utils.o base/strings/utf_string_conversion_utils.cc

base/strings/base_utf_string_conversions.o:base/strings/utf_string_conversions.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/strings/base_utf_string_conversions.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/strings/base_utf_string_conversions.o base/strings/utf_string_conversions.cc

base/synchronization/base_cancellation_flag.o:base/synchronization/cancellation_flag.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/synchronization/base_cancellation_flag.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/synchronization/base_cancellation_flag.o base/synchronization/cancellation_flag.cc

base/synchronization/base_condition_variable_posix.o:base/synchronization/condition_variable_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/synchronization/base_condition_variable_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/synchronization/base_condition_variable_posix.o base/synchronization/condition_variable_posix.cc

base/synchronization/base_waitable_event_posix.o:base/synchronization/waitable_event_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/synchronization/base_waitable_event_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/synchronization/base_waitable_event_posix.o base/synchronization/waitable_event_posix.cc

base/base_sys_info.o:base/sys_info.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_sys_info.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_sys_info.o base/sys_info.cc

base/base_sys_info_linux.o:base/sys_info_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_sys_info_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_sys_info_linux.o base/sys_info_linux.cc

base/base_sys_info_posix.o:base/sys_info_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_sys_info_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_sys_info_posix.o base/sys_info_posix.cc

base/threading/base_non_thread_safe_impl.o:base/threading/non_thread_safe_impl.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_non_thread_safe_impl.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_non_thread_safe_impl.o base/threading/non_thread_safe_impl.cc

base/threading/base_platform_thread_linux.o:base/threading/platform_thread_linux.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_platform_thread_linux.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_platform_thread_linux.o base/threading/platform_thread_linux.cc

base/threading/base_platform_thread_posix.o:base/threading/platform_thread_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_platform_thread_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_platform_thread_posix.o base/threading/platform_thread_posix.cc

base/threading/base_simple_thread.o:base/threading/simple_thread.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_simple_thread.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_simple_thread.o base/threading/simple_thread.cc

base/threading/base_thread_checker_impl.o:base/threading/thread_checker_impl.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_thread_checker_impl.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_thread_checker_impl.o base/threading/thread_checker_impl.cc

base/threading/base_thread_collision_warner.o:base/threading/thread_collision_warner.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_thread_collision_warner.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_thread_collision_warner.o base/threading/thread_collision_warner.cc

base/threading/base_thread_id_name_manager.o:base/threading/thread_id_name_manager.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_thread_id_name_manager.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_thread_id_name_manager.o base/threading/thread_id_name_manager.cc

base/threading/base_thread_local_posix.o:base/threading/thread_local_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_thread_local_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_thread_local_posix.o base/threading/thread_local_posix.cc

base/threading/base_thread_local_storage.o:base/threading/thread_local_storage.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_thread_local_storage.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_thread_local_storage.o base/threading/thread_local_storage.cc

base/threading/base_thread_local_storage_posix.o:base/threading/thread_local_storage_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_thread_local_storage_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_thread_local_storage_posix.o base/threading/thread_local_storage_posix.cc

base/threading/base_thread_restrictions.o:base/threading/thread_restrictions.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_thread_restrictions.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_thread_restrictions.o base/threading/thread_restrictions.cc

base/threading/base_watchdog.o:base/threading/watchdog.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/threading/base_watchdog.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/threading/base_watchdog.o base/threading/watchdog.cc

base/time/base_clock.o:base/time/clock.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/time/base_clock.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/time/base_clock.o base/time/clock.cc

base/time/base_default_clock.o:base/time/default_clock.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/time/base_default_clock.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/time/base_default_clock.o base/time/default_clock.cc

base/time/base_default_tick_clock.o:base/time/default_tick_clock.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/time/base_default_tick_clock.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/time/base_default_tick_clock.o base/time/default_tick_clock.cc

base/time/base_tick_clock.o:base/time/tick_clock.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/time/base_tick_clock.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/time/base_tick_clock.o base/time/tick_clock.cc

base/time/base_time.o:base/time/time.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/time/base_time.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/time/base_time.o base/time/time.cc

base/time/base_time_posix.o:base/time/time_posix.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/time/base_time_posix.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/time/base_time_posix.o base/time/time_posix.cc

base/base_version.o:base/version.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_version.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_version.o base/version.cc

base/base_logging.o:base/logging.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_logging.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_logging.o base/logging.cc

base/base_class_name.o:base/class_name.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_class_name.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_class_name.o base/class_name.cpp

base/base_errno.o:base/errno.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_errno.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_errno.o base/errno.cpp

base/base_find_cstr.o:base/find_cstr.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_find_cstr.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_find_cstr.o base/find_cstr.cpp

base/base_status.o:base/status.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_status.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_status.o base/status.cpp

base/base_string_printf.o:base/string_printf.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_string_printf.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_string_printf.o base/string_printf.cpp

base/base_thread_local.o:base/thread_local.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_thread_local.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_thread_local.o base/thread_local.cpp

base/base_unix_socket.o:base/unix_socket.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_unix_socket.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_unix_socket.o base/unix_socket.cpp

base/base_endpoint.o:base/endpoint.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_endpoint.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_endpoint.o base/endpoint.cpp

base/base_fd_utility.o:base/fd_utility.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_fd_utility.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_fd_utility.o base/fd_utility.cpp

base/files/base_temp_file.o:base/files/temp_file.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_temp_file.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_temp_file.o base/files/temp_file.cpp

base/files/base_file_watcher.o:base/files/file_watcher.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/files/base_file_watcher.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/files/base_file_watcher.o base/files/file_watcher.cpp

base/base_time.o:base/time.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_time.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_time.o base/time.cpp

base/base_zero_copy_stream_as_streambuf.o:base/zero_copy_stream_as_streambuf.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_zero_copy_stream_as_streambuf.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_zero_copy_stream_as_streambuf.o base/zero_copy_stream_as_streambuf.cpp

base/base_crc32c.o:base/crc32c.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_crc32c.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_crc32c.o base/crc32c.cc

base/containers/base_case_ignored_flat_map.o:base/containers/case_ignored_flat_map.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/containers/base_case_ignored_flat_map.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/containers/base_case_ignored_flat_map.o base/containers/case_ignored_flat_map.cpp

base/base_iobuf.o:base/iobuf.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbase/base_iobuf.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o base/base_iobuf.o base/iobuf.cpp

bvar/bvar_collector.o:bvar/collector.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbvar/bvar_collector.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bvar/bvar_collector.o bvar/collector.cpp

bvar/bvar_default_variables.o:bvar/default_variables.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbvar/bvar_default_variables.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bvar/bvar_default_variables.o bvar/default_variables.cpp

bvar/bvar_gflag.o:bvar/gflag.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbvar/bvar_gflag.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bvar/bvar_gflag.o bvar/gflag.cpp

bvar/bvar_latency_recorder.o:bvar/latency_recorder.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbvar/bvar_latency_recorder.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bvar/bvar_latency_recorder.o bvar/latency_recorder.cpp

bvar/bvar_variable.o:bvar/variable.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbvar/bvar_variable.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bvar/bvar_variable.o bvar/variable.cpp

bvar/detail/bvar_percentile.o:bvar/detail/percentile.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbvar/detail/bvar_percentile.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bvar/detail/bvar_percentile.o bvar/detail/percentile.cpp

bvar/detail/bvar_sampler.o:bvar/detail/sampler.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbvar/detail/bvar_sampler.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bvar/detail/bvar_sampler.o bvar/detail/sampler.cpp

bthread/bthread_bthread.o:bthread/bthread.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_bthread.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_bthread.o bthread/bthread.cpp

bthread/bthread_butex.o:bthread/butex.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_butex.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_butex.o bthread/butex.cpp

bthread/bthread_cond.o:bthread/cond.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_cond.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_cond.o bthread/cond.cpp

bthread/bthread_context.o:bthread/context.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_context.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_context.o bthread/context.cpp

bthread/bthread_countdown_event.o:bthread/countdown_event.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_countdown_event.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_countdown_event.o bthread/countdown_event.cpp

bthread/bthread_errno.o:bthread/errno.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_errno.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_errno.o bthread/errno.cpp

bthread/bthread_execution_queue.o:bthread/execution_queue.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_execution_queue.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_execution_queue.o bthread/execution_queue.cpp

bthread/bthread_fd.o:bthread/fd.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_fd.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_fd.o bthread/fd.cpp

bthread/bthread_id.o:bthread/id.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_id.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_id.o bthread/id.cpp

bthread/bthread_interrupt_pthread.o:bthread/interrupt_pthread.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_interrupt_pthread.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_interrupt_pthread.o bthread/interrupt_pthread.cpp

bthread/bthread_key.o:bthread/key.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_key.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_key.o bthread/key.cpp

bthread/bthread_mutex.o:bthread/mutex.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_mutex.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_mutex.o bthread/mutex.cpp

bthread/bthread_stack.o:bthread/stack.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_stack.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_stack.o bthread/stack.cpp

bthread/bthread_sys_futex.o:bthread/sys_futex.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_sys_futex.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_sys_futex.o bthread/sys_futex.cpp

bthread/bthread_task_control.o:bthread/task_control.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_task_control.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_task_control.o bthread/task_control.cpp

bthread/bthread_task_group.o:bthread/task_group.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_task_group.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_task_group.o bthread/task_group.cpp

bthread/bthread_timer_thread.o:bthread/timer_thread.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbthread/bthread_timer_thread.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o bthread/bthread_timer_thread.o bthread/timer_thread.cpp

json2pb/json2pb_encode_decode.o:json2pb/encode_decode.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mjson2pb/json2pb_encode_decode.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o json2pb/json2pb_encode_decode.o json2pb/encode_decode.cpp

json2pb/json2pb_json_to_pb.o:json2pb/json_to_pb.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mjson2pb/json2pb_json_to_pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o json2pb/json2pb_json_to_pb.o json2pb/json_to_pb.cpp

json2pb/json2pb_pb_to_json.o:json2pb/pb_to_json.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mjson2pb/json2pb_pb_to_json.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o json2pb/json2pb_pb_to_json.o json2pb/pb_to_json.cpp

json2pb/json2pb_protobuf_map.o:json2pb/protobuf_map.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mjson2pb/json2pb_protobuf_map.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o json2pb/json2pb_protobuf_map.o json2pb/protobuf_map.cpp

mcpack2pb/mcpack2pb_field_type.o:mcpack2pb/field_type.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mmcpack2pb/mcpack2pb_field_type.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o mcpack2pb/mcpack2pb_field_type.o mcpack2pb/field_type.cpp

mcpack2pb/mcpack2pb_mcpack2pb.o:mcpack2pb/mcpack2pb.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mmcpack2pb/mcpack2pb_mcpack2pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o mcpack2pb/mcpack2pb_mcpack2pb.o mcpack2pb/mcpack2pb.cpp

mcpack2pb/mcpack2pb_parser.o:mcpack2pb/parser.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mmcpack2pb/mcpack2pb_parser.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o mcpack2pb/mcpack2pb_parser.o mcpack2pb/parser.cpp

mcpack2pb/mcpack2pb_serializer.o:mcpack2pb/serializer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mmcpack2pb/mcpack2pb_serializer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o mcpack2pb/mcpack2pb_serializer.o mcpack2pb/serializer.cpp

mcpack2pb_idl_options.pb.o:idl_options.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mmcpack2pb_idl_options.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o mcpack2pb_idl_options.pb.o idl_options.pb.cc

mcpack2pb/protoc-gen-mcpack_generator.o:mcpack2pb/generator.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mmcpack2pb/protoc-gen-mcpack_generator.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o mcpack2pb/protoc-gen-mcpack_generator.o mcpack2pb/generator.cpp

brpc/brpc_acceptor.o:brpc/acceptor.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_acceptor.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_acceptor.o brpc/acceptor.cpp

brpc/brpc_adaptive_connection_type.o:brpc/adaptive_connection_type.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_adaptive_connection_type.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_adaptive_connection_type.o brpc/adaptive_connection_type.cpp

brpc/brpc_amf.o:brpc/amf.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_amf.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_amf.o brpc/amf.cpp

brpc/brpc_bad_method_service.o:brpc/bad_method_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_bad_method_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_bad_method_service.o brpc/bad_method_service.cpp

brpc/brpc_channel.o:brpc/channel.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_channel.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_channel.o brpc/channel.cpp

brpc/brpc_compress.o:brpc/compress.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_compress.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_compress.o brpc/compress.cpp

brpc/brpc_controller.o:brpc/controller.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_controller.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_controller.o brpc/controller.cpp

brpc/brpc_esp_message.o:brpc/esp_message.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_esp_message.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_esp_message.o brpc/esp_message.cpp

brpc/brpc_event_dispatcher.o:brpc/event_dispatcher.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_event_dispatcher.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_event_dispatcher.o brpc/event_dispatcher.cpp

brpc/brpc_global.o:brpc/global.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_global.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_global.o brpc/global.cpp

brpc/brpc_http_header.o:brpc/http_header.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_http_header.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_http_header.o brpc/http_header.cpp

brpc/brpc_http_method.o:brpc/http_method.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_http_method.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_http_method.o brpc/http_method.cpp

brpc/brpc_http_status_code.o:brpc/http_status_code.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_http_status_code.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_http_status_code.o brpc/http_status_code.cpp

brpc/brpc_input_messenger.o:brpc/input_messenger.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_input_messenger.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_input_messenger.o brpc/input_messenger.cpp

brpc/brpc_load_balancer.o:brpc/load_balancer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_load_balancer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_load_balancer.o brpc/load_balancer.cpp

brpc/brpc_load_balancer_with_naming.o:brpc/load_balancer_with_naming.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_load_balancer_with_naming.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_load_balancer_with_naming.o brpc/load_balancer_with_naming.cpp

brpc/brpc_memcache.o:brpc/memcache.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_memcache.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_memcache.o brpc/memcache.cpp

brpc/brpc_naming_service_thread.o:brpc/naming_service_thread.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_naming_service_thread.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_naming_service_thread.o brpc/naming_service_thread.cpp

brpc/brpc_nshead_message.o:brpc/nshead_message.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_nshead_message.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_nshead_message.o brpc/nshead_message.cpp

brpc/brpc_nshead_pb_service_adaptor.o:brpc/nshead_pb_service_adaptor.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_nshead_pb_service_adaptor.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_nshead_pb_service_adaptor.o brpc/nshead_pb_service_adaptor.cpp

brpc/brpc_nshead_service.o:brpc/nshead_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_nshead_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_nshead_service.o brpc/nshead_service.cpp

brpc/brpc_parallel_channel.o:brpc/parallel_channel.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_parallel_channel.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_parallel_channel.o brpc/parallel_channel.cpp

brpc/brpc_partition_channel.o:brpc/partition_channel.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_partition_channel.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_partition_channel.o brpc/partition_channel.cpp

brpc/brpc_periodic_naming_service.o:brpc/periodic_naming_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_periodic_naming_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_periodic_naming_service.o brpc/periodic_naming_service.cpp

brpc/brpc_progressive_attachment.o:brpc/progressive_attachment.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_progressive_attachment.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_progressive_attachment.o brpc/progressive_attachment.cpp

brpc/brpc_protocol.o:brpc/protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_protocol.o brpc/protocol.cpp

brpc/brpc_redis.o:brpc/redis.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_redis.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_redis.o brpc/redis.cpp

brpc/brpc_redis_command.o:brpc/redis_command.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_redis_command.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_redis_command.o brpc/redis_command.cpp

brpc/brpc_redis_reply.o:brpc/redis_reply.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_redis_reply.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_redis_reply.o brpc/redis_reply.cpp

brpc/brpc_reloadable_flags.o:brpc/reloadable_flags.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_reloadable_flags.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_reloadable_flags.o brpc/reloadable_flags.cpp

brpc/brpc_restful.o:brpc/restful.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_restful.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_restful.o brpc/restful.cpp

brpc/brpc_retry_policy.o:brpc/retry_policy.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_retry_policy.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_retry_policy.o brpc/retry_policy.cpp

brpc/brpc_rpc_dump.o:brpc/rpc_dump.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_rpc_dump.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_rpc_dump.o brpc/rpc_dump.cpp

brpc/brpc_rtmp.o:brpc/rtmp.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_rtmp.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_rtmp.o brpc/rtmp.cpp

brpc/brpc_selective_channel.o:brpc/selective_channel.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_selective_channel.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_selective_channel.o brpc/selective_channel.cpp

brpc/brpc_serialized_request.o:brpc/serialized_request.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_serialized_request.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_serialized_request.o brpc/serialized_request.cpp

brpc/brpc_server.o:brpc/server.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_server.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_server.o brpc/server.cpp

brpc/brpc_server_id.o:brpc/server_id.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_server_id.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_server_id.o brpc/server_id.cpp

brpc/brpc_socket.o:brpc/socket.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_socket.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_socket.o brpc/socket.cpp

brpc/brpc_socket_map.o:brpc/socket_map.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_socket_map.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_socket_map.o brpc/socket_map.cpp

brpc/brpc_span.o:brpc/span.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_span.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_span.o brpc/span.cpp

brpc/brpc_stream.o:brpc/stream.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_stream.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_stream.o brpc/stream.cpp

brpc/brpc_tcmalloc_extension.o:brpc/tcmalloc_extension.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_tcmalloc_extension.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_tcmalloc_extension.o brpc/tcmalloc_extension.cpp

brpc/brpc_trackme.o:brpc/trackme.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_trackme.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_trackme.o brpc/trackme.cpp

brpc/brpc_ts.o:brpc/ts.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_ts.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_ts.o brpc/ts.cpp

brpc/brpc_uri.o:brpc/uri.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_uri.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_uri.o brpc/uri.cpp

brpc/policy/brpc_baidu_rpc_protocol.o:brpc/policy/baidu_rpc_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_baidu_rpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_baidu_rpc_protocol.o brpc/policy/baidu_rpc_protocol.cpp

brpc/policy/brpc_consistent_hashing_load_balancer.o:brpc/policy/consistent_hashing_load_balancer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_consistent_hashing_load_balancer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_consistent_hashing_load_balancer.o brpc/policy/consistent_hashing_load_balancer.cpp

brpc/policy/brpc_dh.o:brpc/policy/dh.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_dh.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_dh.o brpc/policy/dh.cpp

brpc/policy/brpc_domain_naming_service.o:brpc/policy/domain_naming_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_domain_naming_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_domain_naming_service.o brpc/policy/domain_naming_service.cpp

brpc/policy/brpc_dynpart_load_balancer.o:brpc/policy/dynpart_load_balancer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_dynpart_load_balancer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_dynpart_load_balancer.o brpc/policy/dynpart_load_balancer.cpp

brpc/policy/brpc_esp_authenticator.o:brpc/policy/esp_authenticator.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_esp_authenticator.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_esp_authenticator.o brpc/policy/esp_authenticator.cpp

brpc/policy/brpc_esp_protocol.o:brpc/policy/esp_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_esp_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_esp_protocol.o brpc/policy/esp_protocol.cpp

brpc/policy/brpc_file_naming_service.o:brpc/policy/file_naming_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_file_naming_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_file_naming_service.o brpc/policy/file_naming_service.cpp

brpc/policy/brpc_gzip_compress.o:brpc/policy/gzip_compress.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_gzip_compress.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_gzip_compress.o brpc/policy/gzip_compress.cpp

brpc/policy/brpc_hasher.o:brpc/policy/hasher.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_hasher.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_hasher.o brpc/policy/hasher.cpp

brpc/policy/brpc_http_rpc_protocol.o:brpc/policy/http_rpc_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_http_rpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_http_rpc_protocol.o brpc/policy/http_rpc_protocol.cpp

brpc/policy/brpc_hulu_pbrpc_protocol.o:brpc/policy/hulu_pbrpc_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_hulu_pbrpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_hulu_pbrpc_protocol.o brpc/policy/hulu_pbrpc_protocol.cpp

brpc/policy/brpc_list_naming_service.o:brpc/policy/list_naming_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_list_naming_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_list_naming_service.o brpc/policy/list_naming_service.cpp

brpc/policy/brpc_locality_aware_load_balancer.o:brpc/policy/locality_aware_load_balancer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_locality_aware_load_balancer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_locality_aware_load_balancer.o brpc/policy/locality_aware_load_balancer.cpp

brpc/policy/brpc_memcache_binary_protocol.o:brpc/policy/memcache_binary_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_memcache_binary_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_memcache_binary_protocol.o brpc/policy/memcache_binary_protocol.cpp

brpc/policy/brpc_mongo_protocol.o:brpc/policy/mongo_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_mongo_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_mongo_protocol.o brpc/policy/mongo_protocol.cpp

brpc/policy/brpc_nova_pbrpc_protocol.o:brpc/policy/nova_pbrpc_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_nova_pbrpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_nova_pbrpc_protocol.o brpc/policy/nova_pbrpc_protocol.cpp

brpc/policy/brpc_nshead_mcpack_protocol.o:brpc/policy/nshead_mcpack_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_nshead_mcpack_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_nshead_mcpack_protocol.o brpc/policy/nshead_mcpack_protocol.cpp

brpc/policy/brpc_nshead_protocol.o:brpc/policy/nshead_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_nshead_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_nshead_protocol.o brpc/policy/nshead_protocol.cpp

brpc/policy/brpc_public_pbrpc_protocol.o:brpc/policy/public_pbrpc_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_public_pbrpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_public_pbrpc_protocol.o brpc/policy/public_pbrpc_protocol.cpp

brpc/policy/brpc_randomized_load_balancer.o:brpc/policy/randomized_load_balancer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_randomized_load_balancer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_randomized_load_balancer.o brpc/policy/randomized_load_balancer.cpp

brpc/policy/brpc_redis_protocol.o:brpc/policy/redis_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_redis_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_redis_protocol.o brpc/policy/redis_protocol.cpp

brpc/policy/brpc_remote_file_naming_service.o:brpc/policy/remote_file_naming_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_remote_file_naming_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_remote_file_naming_service.o brpc/policy/remote_file_naming_service.cpp

brpc/policy/brpc_round_robin_load_balancer.o:brpc/policy/round_robin_load_balancer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_round_robin_load_balancer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_round_robin_load_balancer.o brpc/policy/round_robin_load_balancer.cpp

brpc/policy/brpc_rtmp_protocol.o:brpc/policy/rtmp_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_rtmp_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_rtmp_protocol.o brpc/policy/rtmp_protocol.cpp

brpc/policy/brpc_snappy_compress.o:brpc/policy/snappy_compress.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_snappy_compress.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_snappy_compress.o brpc/policy/snappy_compress.cpp

brpc/policy/brpc_sofa_pbrpc_protocol.o:brpc/policy/sofa_pbrpc_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_sofa_pbrpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_sofa_pbrpc_protocol.o brpc/policy/sofa_pbrpc_protocol.cpp

brpc/policy/brpc_streaming_rpc_protocol.o:brpc/policy/streaming_rpc_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_streaming_rpc_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_streaming_rpc_protocol.o brpc/policy/streaming_rpc_protocol.cpp

brpc/policy/brpc_ubrpc2pb_protocol.o:brpc/policy/ubrpc2pb_protocol.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_ubrpc2pb_protocol.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_ubrpc2pb_protocol.o brpc/policy/ubrpc2pb_protocol.cpp

brpc/builtin/brpc_bthreads_service.o:brpc/builtin/bthreads_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_bthreads_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_bthreads_service.o brpc/builtin/bthreads_service.cpp

brpc/builtin/brpc_common.o:brpc/builtin/common.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_common.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_common.o brpc/builtin/common.cpp

brpc/builtin/brpc_connections_service.o:brpc/builtin/connections_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_connections_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_connections_service.o brpc/builtin/connections_service.cpp

brpc/builtin/brpc_dir_service.o:brpc/builtin/dir_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_dir_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_dir_service.o brpc/builtin/dir_service.cpp

brpc/builtin/brpc_flags_service.o:brpc/builtin/flags_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_flags_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_flags_service.o brpc/builtin/flags_service.cpp

brpc/builtin/brpc_flot_min_js.o:brpc/builtin/flot_min_js.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_flot_min_js.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_flot_min_js.o brpc/builtin/flot_min_js.cpp

brpc/builtin/brpc_get_favicon_service.o:brpc/builtin/get_favicon_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_get_favicon_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_get_favicon_service.o brpc/builtin/get_favicon_service.cpp

brpc/builtin/brpc_get_js_service.o:brpc/builtin/get_js_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_get_js_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_get_js_service.o brpc/builtin/get_js_service.cpp

brpc/builtin/brpc_health_service.o:brpc/builtin/health_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_health_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_health_service.o brpc/builtin/health_service.cpp

brpc/builtin/brpc_hotspots_service.o:brpc/builtin/hotspots_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_hotspots_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_hotspots_service.o brpc/builtin/hotspots_service.cpp

brpc/builtin/brpc_ids_service.o:brpc/builtin/ids_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_ids_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_ids_service.o brpc/builtin/ids_service.cpp

brpc/builtin/brpc_index_service.o:brpc/builtin/index_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_index_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_index_service.o brpc/builtin/index_service.cpp

brpc/builtin/brpc_jquery_min_js.o:brpc/builtin/jquery_min_js.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_jquery_min_js.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_jquery_min_js.o brpc/builtin/jquery_min_js.cpp

brpc/builtin/brpc_list_service.o:brpc/builtin/list_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_list_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_list_service.o brpc/builtin/list_service.cpp

brpc/builtin/brpc_pprof_perl.o:brpc/builtin/pprof_perl.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_pprof_perl.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_pprof_perl.o brpc/builtin/pprof_perl.cpp

brpc/builtin/brpc_pprof_service.o:brpc/builtin/pprof_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_pprof_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_pprof_service.o brpc/builtin/pprof_service.cpp

brpc/builtin/brpc_protobufs_service.o:brpc/builtin/protobufs_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_protobufs_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_protobufs_service.o brpc/builtin/protobufs_service.cpp

brpc/builtin/brpc_rpcz_service.o:brpc/builtin/rpcz_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_rpcz_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_rpcz_service.o brpc/builtin/rpcz_service.cpp

brpc/builtin/brpc_sockets_service.o:brpc/builtin/sockets_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_sockets_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_sockets_service.o brpc/builtin/sockets_service.cpp

brpc/builtin/brpc_sorttable_js.o:brpc/builtin/sorttable_js.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_sorttable_js.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_sorttable_js.o brpc/builtin/sorttable_js.cpp

brpc/builtin/brpc_status_service.o:brpc/builtin/status_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_status_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_status_service.o brpc/builtin/status_service.cpp

brpc/builtin/brpc_threads_service.o:brpc/builtin/threads_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_threads_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_threads_service.o brpc/builtin/threads_service.cpp

brpc/builtin/brpc_vars_service.o:brpc/builtin/vars_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_vars_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_vars_service.o brpc/builtin/vars_service.cpp

brpc/builtin/brpc_version_service.o:brpc/builtin/version_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_version_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_version_service.o brpc/builtin/version_service.cpp

brpc/builtin/brpc_viz_min_js.o:brpc/builtin/viz_min_js.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_viz_min_js.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_viz_min_js.o brpc/builtin/viz_min_js.cpp

brpc/builtin/brpc_vlog_service.o:brpc/builtin/vlog_service.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/builtin/brpc_vlog_service.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/builtin/brpc_vlog_service.o brpc/builtin/vlog_service.cpp

brpc/details/brpc_has_epollrdhup.o:brpc/details/has_epollrdhup.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_has_epollrdhup.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_has_epollrdhup.o brpc/details/has_epollrdhup.cpp

brpc/details/brpc_hpack.o:brpc/details/hpack.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_hpack.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_hpack.o brpc/details/hpack.cpp

brpc/details/brpc_http_message.o:brpc/details/http_message.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_http_message.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_http_message.o brpc/details/http_message.cpp

brpc/details/brpc_http_message_serializer.o:brpc/details/http_message_serializer.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_http_message_serializer.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_http_message_serializer.o brpc/details/http_message_serializer.cpp

brpc/details/brpc_http_parser.o:brpc/details/http_parser.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_http_parser.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_http_parser.o brpc/details/http_parser.cpp

brpc/details/brpc_method_status.o:brpc/details/method_status.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_method_status.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_method_status.o brpc/details/method_status.cpp

brpc/details/brpc_rtmp_utils.o:brpc/details/rtmp_utils.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_rtmp_utils.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_rtmp_utils.o brpc/details/rtmp_utils.cpp

brpc/details/brpc_ssl_helper.o:brpc/details/ssl_helper.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_ssl_helper.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_ssl_helper.o brpc/details/ssl_helper.cpp

brpc/details/brpc_usercode_backup_pool.o:brpc/details/usercode_backup_pool.cpp
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/details/brpc_usercode_backup_pool.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/details/brpc_usercode_backup_pool.o brpc/details/usercode_backup_pool.cpp

brpc/brpc_builtin_service.pb.o:brpc/builtin_service.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_builtin_service.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_builtin_service.pb.o brpc/builtin_service.pb.cc

brpc/brpc_errno.pb.o:brpc/errno.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_errno.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_errno.pb.o brpc/errno.pb.cc

brpc/brpc_get_favicon.pb.o:brpc/get_favicon.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_get_favicon.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_get_favicon.pb.o brpc/get_favicon.pb.cc

brpc/brpc_get_js.pb.o:brpc/get_js.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_get_js.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_get_js.pb.o brpc/get_js.pb.cc

brpc/brpc_nshead_meta.pb.o:brpc/nshead_meta.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_nshead_meta.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_nshead_meta.pb.o brpc/nshead_meta.pb.cc

brpc/brpc_options.pb.o:brpc/options.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_options.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_options.pb.o brpc/options.pb.cc

brpc/brpc_rpc_dump.pb.o:brpc/rpc_dump.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_rpc_dump.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_rpc_dump.pb.o brpc/rpc_dump.pb.cc

brpc/brpc_rtmp.pb.o:brpc/rtmp.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_rtmp.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_rtmp.pb.o brpc/rtmp.pb.cc

brpc/brpc_span.pb.o:brpc/span.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_span.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_span.pb.o brpc/span.pb.cc

brpc/brpc_streaming_rpc_meta.pb.o:brpc/streaming_rpc_meta.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_streaming_rpc_meta.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_streaming_rpc_meta.pb.o brpc/streaming_rpc_meta.pb.cc

brpc/brpc_trackme.pb.o:brpc/trackme.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/brpc_trackme.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/brpc_trackme.pb.o brpc/trackme.pb.cc

brpc/policy/brpc_baidu_rpc_meta.pb.o:brpc/policy/baidu_rpc_meta.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_baidu_rpc_meta.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_baidu_rpc_meta.pb.o brpc/policy/baidu_rpc_meta.pb.cc

brpc/policy/brpc_hulu_pbrpc_meta.pb.o:brpc/policy/hulu_pbrpc_meta.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_hulu_pbrpc_meta.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_hulu_pbrpc_meta.pb.o brpc/policy/hulu_pbrpc_meta.pb.cc

brpc/policy/brpc_mongo.pb.o:brpc/policy/mongo.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_mongo.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_mongo.pb.o brpc/policy/mongo.pb.cc

brpc/policy/brpc_public_pbrpc_meta.pb.o:brpc/policy/public_pbrpc_meta.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_public_pbrpc_meta.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_public_pbrpc_meta.pb.o brpc/policy/public_pbrpc_meta.pb.cc

brpc/policy/brpc_sofa_pbrpc_meta.pb.o:brpc/policy/sofa_pbrpc_meta.pb.cc
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mbrpc/policy/brpc_sofa_pbrpc_meta.pb.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o brpc/policy/brpc_sofa_pbrpc_meta.pb.o brpc/policy/sofa_pbrpc_meta.pb.cc

endif #ifeq ($(shell uname -m),x86_64)


