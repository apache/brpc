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


#include <ostream>
#include <vector>                           // std::vector
#include "butil/string_splitter.h"
#include "bvar/bvar.h"

#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/controller.h"           // Controller
#include "brpc/errno.pb.h"
#include "brpc/server.h"
#include "brpc/builtin/common.h"
#include "brpc/builtin/vars_service.h"

namespace bvar {
DECLARE_bool(quote_vector);
}


namespace brpc {

// TODO(gejun): parameterize.
// This function returns the script to make bvar plot-able.
// The idea: flot graphs were attached to plot-able bvar as the next <div>
// when the html was generated. When user clicks a bvar, send a request to
// server to get the value series of the bvar. When the response comes back,
// plot and show the graph. Requests will be sent to server every 1 second
// until user clicks the bvar and hide the graph.
void PutVarsHeading(std::ostream& os, bool expand_all) {
    os << "<script language=\"javascript\" type=\"text/javascript\" src=\"/js/jquery_min\"></script>\n"
        "<script language=\"javascript\" type=\"text/javascript\" src=\"/js/flot_min\"></script>\n"
       << TabsHead()
       << "<style type=\"text/css\">\n"
        "#layer1 { margin:0; padding:0; width:1111px; }\n"
        // style of plot-able bvar
        ".variable {\n"
        "  margin:0px;\n"
        "  color:#000000;\n"
        "  cursor:pointer;\n"
        "  position:relative;\n"
        "  background-color:#ffffff;\n"
        "}\n"
        // style of non-plot-able bvar, the difference with .variable is only
        // the cursor .
        ".nonplot-variable {\n"
        "  margin:0px;\n"
        "  color:#000000;\n"
        "  position:relative;\n"
        "  background-color:#ffffff;\n"
        "}\n"
        // style of <p>
        "p {padding: 2px 0; margin: 0px; }\n"
        // style of container of flot graph.
        ".detail {\n"
        "  margin: 0px;\n"
        "  width: 800px;\n"
        "  background-color:#fafafa;\n"
        "}\n"
        ".flot-placeholder {\n"
        "  width: 800px;\n"
        "  height: 200px;\n"
        "  line-height: 1.2em;\n"
        "}\n"
        "</style>\n"
        
        "<script type=\"text/javascript\">\n"
        // Mark if a bvar was ever clicked.
        "var everEnabled = {}\n"
        // Mark if a bvar was enabled ploting
        "var enabled = {}\n"
        // the bvar under cursor
        "var hovering_var = \"\"\n"
        // timeout id of last server call.
        "var timeoutId = {}\n"
        // last plot of the bvar.
        "var lastPlot = {}\n"

        "function prepareGraphs() {\n"
        // Hide all graphs at first.
        "  $(\".detail\").hide();\n"

        // Register clicking functions.
        "  $(\".variable\").click(function() {\n"
        "    var mod = $(this).next(\".detail\");\n"
        "    mod.slideToggle(\"fast\");\n"
        "    var var_name = mod.children(\":first-child\").attr(\"id\");\n"
        "    if (!everEnabled[var_name]) {\n"
        "      everEnabled[var_name] = true;\n"
        // Create tooltip at first click.
        "      $(\"<div id='tooltip-\" + var_name + \"'></div>\").css({\n"
        "        position: \"absolute\",\n"
        "        display: \"none\",\n"
        "        border: \"1px solid #fdd\",\n"
        "        padding: \"2px\",\n"
        "        \"background-color\": \"#ffffca\",\n"
        "        opacity: 0.80\n"
        "      }).appendTo(\"body\");\n"
        // Register hovering event and show the tooltip when event occurs.
        "      $(\"#\" + var_name).bind(\"plothover\", function(event, pos, item) {\n"
        "        if (item) {\n"
        "          hovering_var = var_name;\n"
        "          var thePlot = lastPlot[var_name];\n"
        "          if (thePlot != null) {\n"
        "            item.series.color = \"#808080\";\n"
        "            thePlot.draw();\n"
        "          }\n"
        "          var x = item.datapoint[0];\n"
        "          var y = item.datapoint[1];\n"
        "          $(\"#tooltip-\" + var_name)\n"
        "            .html(y + \"<br/>(\" + describeX(x, item.series) + \")\")\n"
        "            .css({top: item.pageY+5, left: item.pageX+15})\n"
        "            .show();\n"
        "        } else {\n"
        "            hovering_var = \"\";\n"
        "            $(\"#tooltip-\" + var_name).hide();\n"
        "        }\n"
        "      });\n"
        // Register mouseleave to make sure the tooltip is hidden when cursor
        // is out.
        "      $(\"#\" + var_name).bind(\"mouseleave\", function() {\n"
        "        $(\"#tooltip-\" + var_name).hide();\n"
        "      });\n"
        "    }\n"
        "    if (!enabled[var_name]) {\n"
        "      enabled[var_name] = true;\n"
        "      fetchData(var_name);\n"
        "    } else {\n"
        "      enabled[var_name] = false;\n"
        "      clearTimeout(timeoutId[var_name]);\n"
        "    }\n"
        "  });\n"
       << (expand_all ?
        "  $(\".variable\").click();\n" :
         // Set id to "default_expand" to make the graph expand by default.
         // E.g. latency and qps in /status page are expanded by default.
        "  $(\".default_expand\").click();\n") <<
        "}\n"
        
        // options for ploting.
        "var trendOptions = {\n"
        "  colors: ['#F0D06E','#F0B06E','#F0A06E','#F0906E','#F0806E'],\n"
        "  legend: {show:false},\n"
        "  grid: {hoverable:true},\n"
        "  xaxis: { \n"
        "    \"ticks\": [[29,\"-1 day\"],[53,\"-1 hour\"],[113,\"-1 minute\"]]\n"
        "  }\n"
        "}\n"
        "var cdfOptions = {\n"
        "  grid: {hoverable: true},\n"
        "  lines: {\n"
        "    show: true,\n"
        "    fill: true\n"
        "  },\n"
        "  xaxis: {\n"
        "    \"ticks\": [[10,\"10%\"],[20,\"20%\"],[30,\"30%\"],[40,\"40%\"]\n"
        "               ,[50,\"50%\"],[60,\"60%\"],[70,\"70%\"],[80,\"80%\"]\n"
        "               ,[90,\"90%\"],[101,\"99.99%\"]]\n"
        "  }\n"
        "}\n"

        // Show x in tooltip intuitively.
        "function describeTrendX(x) {\n"
        "  if (x >= 173) {\n"
        "    return \"just now\";\n"
        "  } else if (x > 113) {\n"
        "    return (x - 173) + \" second\";\n"
        "  } else if (x > 53) {\n"
        "    return (x - 114) + \" minute\";\n"
        "  } else if (x > 29) {\n"
        "    return (x - 54) + \" hour\";\n"
        "  } else {\n"
        "    return (x - 30) + \" day\";\n"
        "  }\n"
        "}\n"
        "function describeCDFX(x) {\n"
        "  if (x <= 99) {\n"
        "    return x + '%';\n"
        "  } else if (x == 100) {\n"
        "    return '99.9%';\n"
        "  } else if (x == 101) {\n"
        "    return '99.99%';\n"
        "  } else {\n"
        "    return 'unknown ' + x;\n"
        "  }\n"
        "}\n"
        "function describeX(x, series) {\n"
        "  if (series.data[series.data.length-1][0] == 173) {\n"
        "    if (series.label != null) {\n"
        "      return series.label + ' ' + describeTrendX(x);\n"
        "    } else {\n"
        "      return describeTrendX(x);\n"
        "    }\n"
        "  } else if (series.data[series.data.length-1][0] == 101) {\n"
        "    return describeCDFX(x);\n"
        "  } else {\n"
        "    return x;\n"
        "  }\n"
        "}\n"
        // Get value series of bvar from server.
        "function fetchData(var_name) {\n"
        "  function onDataReceived(series) {\n"
        "    if (hovering_var != var_name) {\n"
        "      if (series.label == 'trend') {\n"
        "        lastPlot[var_name] = $.plot(\"#\" + var_name, [series.data], trendOptions);\n"
        "        $(\"#value-\" + var_name).html(series.data[series.data.length - 1][1]);\n"
        "      } else if (series.label == 'cdf') {\n"
        "        lastPlot[var_name] = $.plot(\"#\" + var_name, [series.data], cdfOptions);\n"
        "        $(\"#value-\" + var_name).html(series.data[series.data.length - 1][1]);\n"
        "      } else {\n"
        "        lastPlot[var_name] = $.plot(\"#\" + var_name, series, trendOptions);\n"
       << (bvar::FLAGS_quote_vector ?
        "        var newValue = '\"[';\n" :
        "        var newValue = '[';\n") <<
        "        var i;\n"
        "        for (i = 0; i < series.length; ++i) {\n"
        "            if (i != 0) newValue += ',';\n"
        "            var data = series[i].data;\n"
        "            newValue += data[data.length - 1][1];\n"
        "        }\n"
       << (bvar::FLAGS_quote_vector ?
        "        newValue += ']\"';\n" :
        "        newValue += ']';\n") <<
        "        $(\"#value-\" + var_name).html(newValue);\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  $.ajax({\n"
        "    url: \"/vars/\" + var_name + \"?series\",\n"
        "    type: \"GET\",\n"
        "    dataType: \"json\",\n"
        "    success: onDataReceived\n"
        "  });\n"
        "  if (enabled[var_name]) {\n"
        "    timeoutId[var_name] = setTimeout(function(){ fetchData(var_name); }, 1000);\n"
        "  }\n"
        "}\n"
        "$(prepareGraphs);\n"
        "</script>\n";
}

// We need the space before colon so that user does not have to remove
// trailing colon from $1
static const std::string VAR_SEP = " : ";

class VarsDumper : public bvar::Dumper {
public:
    explicit VarsDumper(butil::IOBufBuilder& os, bool use_html)
        : _os(os), _use_html(use_html) {}
    
    bool dump(const std::string& name, const butil::StringPiece& desc) {
        bool plot = false;
        if (_use_html) {
            bvar::SeriesOptions series_options;
            series_options.test_only = true;
            const int rc = bvar::Variable::describe_series_exposed(
                name, _os, series_options);
            plot = (rc == 0);
            if (plot) {
                _os << "<p class=\"variable\">";
            } else {
                _os << "<p class=\"nonplot-variable\">";
            }
        }
        _os << name << VAR_SEP;
        if (_use_html) {
            _os << "<span id=\"value-" << name << "\">";
        }
        _os << desc;
        if (_use_html) {
            _os << "</span></p>\n";
            if (plot) {
                _os << "<div class=\"detail\"><div id=\"" << name
                     << "\" class=\"flot-placeholder\"></div></div>\n";
            }
        } else {
            _os << "\r\n";
        }
        return true;
    }

    void move_to(butil::IOBuf& buf) {
        _os.move_to(buf);
    }
    
private:
    DISALLOW_COPY_AND_ASSIGN(VarsDumper);
    
    butil::IOBufBuilder & _os;
    bool _use_html;
};

void VarsService::default_method(::google::protobuf::RpcController* cntl_base,
                                 const ::brpc::VarsRequest*,
                                 ::brpc::VarsResponse*,
                                 ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);    
    if (cntl->http_request().uri().GetQuery("series") != NULL) {
        butil::IOBufBuilder os;
        bvar::SeriesOptions series_options;
        const int rc = bvar::Variable::describe_series_exposed(
            cntl->http_request().unresolved_path(), os, series_options);
        if (rc == 0) {
            cntl->http_response().set_content_type("application/json");
            os.move_to(cntl->response_attachment());
        } else if (rc < 0) {
            cntl->SetFailed(ENOMETHOD, "Fail to find any bvar by `%s'",
                            cntl->http_request().unresolved_path().c_str());
        } else {
            cntl->SetFailed(ENODATA, "`%s' does not have value series",
                            cntl->http_request().unresolved_path().c_str());
        }
        return;
    }
    const bool use_html = UseHTML(cntl->http_request());
    bool with_tabs = false;
    if (use_html && cntl->http_request().uri().GetQuery("dataonly") == NULL) {
        with_tabs = true;
    }
    cntl->http_response().set_content_type(
        use_html ? "text/html" : "text/plain");

    butil::IOBufBuilder os;
    if (with_tabs) {
        os << "<!DOCTYPE html><html><head>\n"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n";
        PutVarsHeading(os, cntl->http_request().uri().GetQuery("expand"));
        os << "<script type=\"text/javascript\">\n"
            "const delayTime = 200;\n"
            "var searching = false;\n"
            "function toURL(text) {\n"
            "  if (text == '') {\n"
            "    return '/vars';\n"
            "  }\n"
            // Normalize ;,\s\* to space, trim beginning/ending spaces and
            // replace all spaces with *;* and add beginning/ending *
            //   iobuf,bthread         -> *iobuf*;*bthread*
            //   iobuf,                -> *iobuf*
            //   ;,iobuf               -> *iobuf*
            //   ,;*iobuf*, bthread;,; -> *iobuf*;*bthread*
            "  text = text.replace(/(;|,|\\s|\\*)+/g, ' ').trim()"
            "             .replace(/ /g, '*;*');\n"
            "  if (text == '') {\n"
            "    return '/vars';\n"
            "  }\n"
            "  return '/vars/*' + text + '*';\n"
            "}\n"
            "function onDataReceived(searchText, data) {\n"
            "  for (var var_name in enabled) {\n"
            "    if (enabled[var_name]) {\n"
            "      clearTimeout(timeoutId[var_name]);\n"
            "    }\n"
            "    enabled = {};\n"
            "    everEnabled = {};\n"
            "  }\n"
            "  $(\".detail\").hide();\n"
            "  $('#layer1').html(data);\n"
            "  prepareGraphs();\n"
            "  window.history.pushState('', '', toURL(searchText));\n"
            "  var newSearchText = $('#searchbox').val();\n"
            "  if (newSearchText != searchText) {\n"
            "    setTimeout(search, delayTime);\n"
            "    console.log('text changed during searching, search again');\n"
            "  } else {\n"
            "    searching = false;\n"
            "  }\n"
            "}\n"
            "function search() {\n"
            "  var searchText = $('#searchbox').val();\n"
            "  $.ajax({\n"
            "    url: toURL(searchText) + '?dataonly',\n"
            "    type: \"GET\",\n"
            "    dataType: \"html\",\n"
            "    success: function(data) { onDataReceived(searchText, data); },\n"
            "    error: function(xhr, ajaxOptions, thrownError) {\n"
            "             onDataReceived(searchText, xhr.responseText);\n"
            "           }\n"
            "  });\n"
            "}\n"
            "function onQueryChanged() {\n"
            "  if (searching) {\n"
            "    return;\n"
            "  }\n"
            "  searching = true;\n"
            "  setTimeout(search, delayTime);\n"
            "}\n"
            "</script>\n"
            "</head>\n<body>\n";
        cntl->server()->PrintTabsBody(os, "vars");
        os << "<p>Search : <input id='searchbox' type='text'"
            " onkeyup='onQueryChanged()'></p>"
            "<div id=\"layer1\">\n";
    }    
    VarsDumper dumper(os, use_html);
    bvar::DumpOptions options;
    options.question_mark = '$';
    options.display_filter = 
        (use_html ? bvar::DISPLAY_ON_HTML : bvar::DISPLAY_ON_PLAIN_TEXT);
    options.white_wildcards = cntl->http_request().unresolved_path();
    const int ndump = bvar::Variable::dump_exposed(&dumper, &options);
    if (ndump < 0) {
        cntl->SetFailed("Fail to dump vars");
        return;
    }
    if (!options.white_wildcards.empty() && ndump == 0) {
        cntl->SetFailed(ENOMETHOD, "Fail to find any bvar by `%s'",
                        options.white_wildcards.c_str());
    }
    if (with_tabs) {
        os << "</div></body></html>";
    }
    os.move_to(cntl->response_attachment());
    cntl->set_response_compress_type(COMPRESS_TYPE_GZIP);
}

void VarsService::GetTabInfo(TabInfoList* info_list) const {
    TabInfo* info = info_list->add();
    info->path = "/vars";
    info->tab_name = "vars";
}

} // namespace brpc
