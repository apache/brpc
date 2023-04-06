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


#ifndef BRPC_BUILTIN_TABBED_H
#define BRPC_BUILTIN_TABBED_H

#include <string>
#include <vector>
#include <memory>

namespace brpc {

// Contain the information for showing a tab.
struct TabInfo {
    TabInfo() : order(0) {}

    std::string tab_name;
    std::string path;
    int order;

    TabInfo * add() {
        _list.push_back(std::shared_ptr<TabInfo>(new TabInfo));
        return _list.back().get();
    }
    size_t size() const { return _list.size(); }
    const TabInfo* operator[](size_t i) const { return _list[i].get(); }

    bool valid() const { return !tab_name.empty() && (!path.empty() || !_list.empty()); }

private:
    std::vector<std::shared_ptr<TabInfo>> _list;
};

// For appending TabInfo
class TabInfoList {
public:
    TabInfoList() {}
    TabInfo* add() {
        _list.resize(_list.size() + 1);
        return &_list[_list.size() - 1];
    }
    size_t size() const { return _list.size(); }
    const TabInfo& operator[](size_t i) const { return _list[i]; }
    void resize(size_t newsize) { _list.resize(newsize); }
private:
    TabInfoList(const TabInfoList&);
    void operator=(const TabInfoList&);
    std::vector<TabInfo> _list;
};

// Inherit this class to show the service with one or more tabs.
// NOTE: tabbed services are not shown in /status.
// Example:
//   #include <brpc/builtin/common.h>
//   ...
//   void MySerivce::GetTabInfo(brpc::TabInfoList* info_list) const {
//     brpc::TabInfo* info = info_list->add();
//     info->tab_name = "my_tab";
//     info->path = "/MyService/MyMethod";
//   }
//   void MyService::MyMethod(::google::protobuf::RpcController* controller,
//                            const XXXRequest* request,
//                            XXXResponse* response,
//                            ::google::protobuf::Closure* done) {
//     ...
//     if (use_html) {
//       os << "<!DOCTYPE html><html><head>\n"
//          << "<script language=\"javascript\" type=\"text/javascript\" src=\"/js/jquery_min\"></script>\n"
//          << brpc::TabsHead() << "</head><body>";
//       cntl->server()->PrintTabsBody(os, "my_tab");
//     }
//     ...
//     if (use_html) {
//       os << "</body></html>";
//     }
//   }
// Note: don't forget the jquery.
class Tabbed {
public:
    virtual ~Tabbed() = default;
    virtual void GetTabInfo(TabInfoList* info_list) const = 0;
};

} // namespace brpc

namespace std {
template<>
struct less<const brpc::TabInfo *> {
    bool operator()(const brpc::TabInfo * const lhs, const brpc::TabInfo * const rhs) {
        return lhs->order < rhs->order;
    }
};
}  // namespace std

#endif // BRPC_BUILTIN_TABBED_H
