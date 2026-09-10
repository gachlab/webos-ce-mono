// @@@LICENSE
//
//      Copyright (c) 2010-2012 Hewlett-Packard Development Company, L.P.
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

(function(target) {
    function makeLogFunction (messageLevel) {
        function logImplementation () {
        	var stringToLog;
        	var args = [],i,count=arguments.length;
        	for(i = 0; i < count; ++i) {
        	    args.push(arguments[i]);
        	}
    		var formatString = args.shift();
    		if (formatString) {
    		  // make sure the format string is in fact a string
    		  formatString = "" + formatString;
    			var nextArgument = function(stringToReplace) {
    				var target;
    				if (stringToReplace === "%%") {
    					return  "%";
    				}

    				target = args.shift();
    				switch (stringToReplace) {
    				case "%j":
    					return JSON.stringify(target);
    				}

    				return target;
    			};
    			var resultString = formatString.replace(/%[jsdfio%]/g, nextArgument);
    			stringToLog = [resultString].concat(args).join(" ");
				target._logString(messageLevel, target.name||"<>", stringToLog);				
    		}
        	return stringToLog;
        };
        return logImplementation;
    }
    target.error = makeLogFunction(target.LOG_ERR)
    target.warn = makeLogFunction(target.LOG_WARNING)
    target.info = makeLogFunction(target.LOG_INFO)
    target.log = makeLogFunction(target.LOG_INFO)
})
