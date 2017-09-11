import json
import urllib2
data = { "message" : "hello world" }
request_json = json.dumps(data)
req = urllib2.Request("http://127.0.0.1:8000/EchoService/Echo")
try:
    response = urllib2.urlopen(req, request_json, 1)
    print response.read()
except urllib2.HTTPError as e:
    print e.exception.code
