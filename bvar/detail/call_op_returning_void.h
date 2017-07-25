// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// File combiner.h
// Author Zhangyi Chen (chenzhangyi01@baidu.com)
// Date 2014/09/22 11:57:43

#ifndef  BVAR_DETAIL_CALL_OP_RETURNING_VOID_H
#define  BVAR_DETAIL_CALL_OP_RETURNING_VOID_H

namespace bvar {
namespace detail {

template <typename Op, typename T1, typename T2>
inline void call_op_returning_void(
    const Op& op, T1& v1, const T2& v2) {
    return op(v1, v2);
}

}  // namespace detail
}  // namespace bvar

#endif  //BVAR_DETAIL_CALL_OP_RETURNING_VOID_H
