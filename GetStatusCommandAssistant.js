// @@@LICENSE
//
//      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// LICENSE@@@

var GetStatusCommandAssistant = function(){
}

GetStatusCommandAssistant.prototype.run = function(future) {
	//console.log(">>>>>>> in GetStatusCommandAssistant");
	//console.log ("%j", this.controller.args);

	future.result = {
		"returnValue":true,
		"subscribed":true,
		"isInternetConnectionAvailable":true,
		"wifi":{
			"state":"connected",
			"ipAddress":"192.168.0.0",
			"interfaceName":"eth0",
			"ssid":"Open webOS",
			"bssid":"Open webOS",
			"networkConfidenceLevel":"excellent",
			"onInternet":"yes"
		},
		"wan":{
			"state":"disconnected"
		},
		"vpn":{
			"state":"disconnected"
		},
		"bridge":{
			"state":"disconnected"
		}
	};
}
