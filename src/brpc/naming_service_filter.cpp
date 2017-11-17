// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Kevin.XU (xuhuahai@sogou-inc.com)

#include <json/json.h>
#include "naming_service_filter.h"

namespace brpc {

void DefaultNamingServiceFilter::RegisterBasicFilter(BasicServiceFilter* basicServiceFilter){
    _filters.push_back(basicServiceFilter);
}

bool DefaultNamingServiceFilter::Accept(const ServerNode& server) const{
    Json::Reader reader;
    Json::Value rootElement;
    reader.parse(server.tag, rootElement, false);
    Json::Value NULLVALUE;
    std::vector<std::pair<std::string,std::string> > tags;
    if(!rootElement.empty()){
        Json::Value::Members names = rootElement.getMemberNames();
        Json::Value::Members::iterator nameIter = names.begin();
        for(;nameIter!=names.end();++nameIter){
            std::string name = *nameIter;
            const Json::Value & valueElement = rootElement.get(name,NULLVALUE);
            if(valueElement != NULLVALUE){
                tags.push_back( std::make_pair(name,valueElement.asString()) );
            }
        }
    }
    std::vector<BasicServiceFilter *>::const_iterator it = _filters.cbegin();
    for(;it!=_filters.cend();++it){
        BasicServiceFilter * filter = *it;
        if(!filter->Accept(tags)){  // Only one filter don't match , means not match
            return false;
        }
    }
    return true;
}

bool DefaultBasicServiceFilter::Accept(const std::vector<std::pair<std::string,std::string> > &tags) const{
    if(tags.empty()){
        return false;
    }
    std::vector<std::pair<std::string,std::string> >::const_iterator it = tags.cbegin();
    for(;it!=tags.cend();++it){
        const std::pair<std::string,std::string>& tagPair = *it;
        const std::string & tagName = tagPair.first;
        const std::string & tagValue = tagPair.second;
        if(tagName == _name && tagValue == _value){  // Found expected tag
            return true;
        }
    }
    return false;
}

} // namespace brpc
