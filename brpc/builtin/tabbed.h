// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Dec 21 16:49:11 CST 2015

#ifndef BRPC_BUILTIN_TABBED_H
#define BRPC_BUILTIN_TABBED_H

#include <string>
#include <vector>


namespace brpc {

// Contain the information for showing a tab.
struct TabInfo {
    std::string tab_name;
    std::string path;

    bool valid() const { return !tab_name.empty() && !path.empty(); }
};

// For appending TabInfo
class TabInfoList {
public:
    TabInfoList() {}
    TabInfo* add() {
        _list.push_back(TabInfo());
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
//
// Note: don't forget the jquery.
class Tabbed {
public:
    virtual void GetTabInfo(TabInfoList* info_list) const = 0;
};

} // namespace brpc


#endif // BRPC_BUILTIN_TABBED_H
