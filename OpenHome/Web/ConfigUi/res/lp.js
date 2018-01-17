WebUi = function() {

    var gLongPoll = null;
    var gStarted = false;

    // Create a wrapper for XMLHttpRequest that will automatically include any
    // URI resource prefix in open() calls.
    var HttpRequest = function(aId, aType)
    {
        this.iId = aId;
        this.iType = aType;
        this.iRequest = null;
        this.iPrefix = HttpRequest.GetResourcePrefix();
        // XMLHttpRequest construction taken from
        // http://www.w3schools.com/ajax/ajax_xmlhttprequest_create.asp
        if (window.XMLHttpRequest)
        {// code for IE7+, Firefox, Chrome, Opera, Safari
            this.iRequest = new XMLHttpRequest();
        }
        else
        {// code for IE6, IE5
            this.iRequest = new ActiveXObject("Microsoft.XMLHTTP");
        }
    }

    HttpRequest.EType = {
        eCreate     : "Initialising long polling",
        eLongPoll   : "Long polling",
        eTerminate  : "Terminating long polling",
        eUpdate     : "Sending update",
    };

    HttpRequest.prototype.Id = function()
    {
        return this.iId;
    }

    HttpRequest.prototype.Type = function()
    {
        return this.iType;
    }

    HttpRequest.prototype.ReadyState = function()
    {
        return this.iRequest.readyState;
    }

    HttpRequest.prototype.Status = function()
    {
        return this.iRequest.status;
    }

    HttpRequest.prototype.Open = function(aMethod, aUrl, aAsync)
    {
        this.iRequest.open(aMethod, aUrl, aAsync);
    }

    HttpRequest.prototype.SetRequestHeader = function(aHeader, aValue)
    {
        this.iRequest.setRequestHeader(aHeader, aValue);
    }

    HttpRequest.prototype.Send = function(aString)
    {
        this.iRequest.send(aString);
    }

    HttpRequest.prototype.ResponseUrl = function()
    {
        return this.iRequest.responseURL;
    }

    HttpRequest.prototype.ResponseText = function()
    {
        return this.iRequest.responseText;
    }

    HttpRequest.prototype.SetOnReadyStateChange = function(aCallbackResponse)
    {
        this.iRequest.onreadystatechange = aCallbackResponse;
    }

    HttpRequest.prototype.AddEventListener = function(aEvent, aCallback, aCapture)
    {
        this.iRequest.addEventListener(aEvent, aCallback, aCapture);
    }

    HttpRequest.prototype.SetTimeout = function(aTimeoutMs)
    {
        this.iRequest.timeout = aTimeoutMs;
    }

    HttpRequest.prototype.Abort = function()
    {
        this.iRequest.abort();
    }

    HttpRequest.GetResourcePrefix = function()
    {
        var url = document.URL;
        var urlSplit = url.split("/");
        if (urlSplit.length > 1) {
            return urlSplit[urlSplit.length-2];
        }
        else {
            return "";
        }
    }


    var LongPoll = function(aCallbackStarted, aCallbackSuccess, aCallbackFailure)
    {
        this.kRetryTimeoutMs = 1000;
        this.kSessionIdStart = 0;
        this.kSessionIdInvalid = Number.MAX_SAFE_INTEGER;
        this.kResponseTimeoutMs = 10000; // Long poll duration is 5s. Pick a time greater than that to avoid false-positive timeouts.

        this.iSessionId = this.kSessionIdStart;
        this.iNextRequestId = 1;
        this.iPendingRequestsLongPoll = [];
        this.iPendingRequestsUpdate = [];
        this.iCallbackStarted = aCallbackStarted;
        this.iCallbackSuccess = aCallbackSuccess;
        this.iCallbackFailure = aCallbackFailure;
    }

    LongPoll.prototype.NewCreateRequest = function()
    {
        var request = new HttpRequest(this.iNextRequestId++, HttpRequest.EType.eCreate);
        this.AddRequest(request);
        return request;
    }

    LongPoll.prototype.NewLongPollRequest = function()
    {
        var request = new HttpRequest(this.iNextRequestId++, HttpRequest.EType.eLongPoll);
        this.AddRequest(request);
        return request;
    }

    LongPoll.prototype.NewTerminateRequest = function()
    {
        var request = new HttpRequest(this.iNextRequestId++, HttpRequest.EType.eTerminate);
        this.AddRequest(request); // Ensures all pending "long poll" calls are cancelled before this is sent. Especially important when trying to terminate a session, as don't want a pending long poll request to trigger another "lp" or a new "lpcreate" request (which could then override this "lpterminate" request).
        return request;
    }

    LongPoll.prototype.NewUpdateRequest = function()
    {
        var request = new HttpRequest(this.iNextRequestId++, HttpRequest.EType.eUpdate);
        this.AddRequest(request);
        return request;
    }

    LongPoll.prototype.AddRequest = function(aRequest)
    {
        if (aRequest.Type() == HttpRequest.EType.eCreate
                || aRequest.Type() == HttpRequest.EType.eLongPoll
                || aRequest.Type() == HttpRequest.EType.eTerminate) {
            console.log("LongPoll.AddRequest long poll request");
            this.AddLongPollRequest(aRequest);
        }
        else if (aRequest.Type() == HttpRequest.EType.eUpdate) {
            console.log("LongPoll.AddRequest update request");
            this.AddUpdateRequest(aRequest);
        }
    }

    LongPoll.prototype.AddLongPollRequest = function(aRequest)
    {
        // There should only ever be one "long polling" message (this covers
        // "lpcreate", "lp" and "lpterminate") active at any time during a long
        // polling session.
        this.AbortAllLongPollRequests();
        this.iPendingRequestsLongPoll.push(aRequest);
    }

    LongPoll.prototype.AddUpdateRequest = function(aRequest)
    {
        this.iPendingRequestsUpdate.push(aRequest);
    }

    /*
    LongPoll.prototype.RemoveRequest = function(aRequest)
    {
        var pos = this.iPendingRequests.findIndex(
            function(aElement, aIndex, aArray) {
                return aElement.Id() == aRequest.Id();
            }
        );

        if (pos > -1) {
            this.iPendingRequests.splice(pos, 1);
        }
    }
    */

    LongPoll.prototype.PendingLongPollRequestIndex = function(aRequest)
    {
        // Returns index of aRequest if it is in pending requests list; -1 otherwise.
        return this.iPendingRequestsLongPoll.findIndex(
            function(aElement, aIndex, aArray) {
                return aElement.Id() == aRequest.Id();
            }
        );
    }

    LongPoll.prototype.PendingUpdateRequestIndex = function(aRequest)
    {
        // Returns index of aRequest if it is in pending requests list; -1 otherwise.
        return this.iPendingRequestsUpdate.findIndex(
            function(aElement, aIndex, aArray) {
                return aElement.Id() == aRequest.Id();
            }
        );
    }

    LongPoll.prototype.AbortAllLongPollRequests = function()
    {
        console.log("LongPoll.AbortAllLongPollRequests iPendingRequestsLongPoll.length: %d", this.iPendingRequestsLongPoll.length);
        while (this.iPendingRequestsLongPoll.length > 0) {
            var req = this.iPendingRequestsLongPoll.shift();
            req.Abort();
        }
    }

    LongPoll.prototype.AbortAllUpdateRequests = function()
    {
        console.log("LongPoll.AbortAllUpdateRequests iPendingRequestsUpdate.length: %d", this.iPendingRequestsUpdate.length);
        while (this.iPendingRequestsUpdate.length > 0) {
            var req = this.iPendingRequestsUpdate.shift();
            req.Abort();
        }
    }

    LongPoll.prototype.AbortAllRequests = function()
    {
        this.AbortAllLongPollRequests();
        this.AbortAllUpdateRequests();
    }

    LongPoll.prototype.ResponseSuccess = function(aRequest)
    {
        console.log("LongPoll.ResponseSuccess aRequest.Id(): %d", aRequest.Id());
        var longPoll = this;
        var longPollRequestIdx = this.PendingLongPollRequestIndex(aRequest);
        var updateRequestIdx = this.PendingUpdateRequestIndex(aRequest);
        if (longPollRequestIdx > -1) {
            console.log("LongPoll.ResponseSuccess from pending long poll request: %s", aRequest.Type());
            // Long poll request is in pending list. Act on it.
            this.iPendingRequestsLongPoll.splice(longPollRequestIdx, 1);

            if (aRequest.Type() == HttpRequest.EType.eCreate) {
                console.log("LongPoll.ResponseSuccess received reponse to eCreate call. Sending eLongPoll");
                setTimeout(function SendPollCallback() {longPoll.SendPoll();}, 1);
            }
            else if (aRequest.Type() == HttpRequest.EType.eLongPoll) {
                console.log("LongPoll.ResponseSuccess received reponse to eLongPoll call. Sending eLongPoll");
                setTimeout(function SendPollCallback() {longPoll.SendPoll();}, 1);
            }
            else {
                // Nothing further to do for eTerminate types.
            }
        }
        else if (updateRequestIdx > -1) {
            console.log("LongPoll.ResponseSuccess from pending update request: %s", aRequest.Type());
            // Update request is in pending list. Remove it.
            // Nothing further to do.
            this.iPendingRequestsUpdate.splice(updateRequestIdx, 1);
        }
        else {
            console.log("LongPoll.ResponseSuccess from non-pending request: %s", aRequest.Type());
        }
    }

    LongPoll.prototype.ResponseError = function(aRequest)
    {
        var longPoll = this;
        if (this.PendingLongPollRequestIndex(aRequest) > -1
                || this.PendingUpdateRequestIndex(aRequest) > -1) {
            console.log("LongPoll.ResponseError from pending request: %s", aRequest.Type());
            // Request is in pending list. Act on it.
            //this.iPendingRequests.splice(pos, 1);
            // If any request fails (including an update) want to restart long
            // polling. Cancel all outstanding requests.
            this.AbortAllRequests();

            // Current message type doesn't actually matter. For any failure
            // (including a failure to send an update), we're going to attempt
            // to recreate the long polling session.
            this.iSessionId = this.kSessionIdStart;
            console.log("LongPoll.ResponseError sending new eCreate in %d ms", this.kRetryTimeoutMs);
            setTimeout(function SendCreateCallback() {longPoll.SendCreate();}, this.kRetryTimeoutMs);
        }
        else {
            console.log("LongPoll.ResponseError from non-pending request: %s", aRequest.Type());
        }
    }

    LongPoll.prototype.Terminate = function()
    {
        this.AbortAllRequests();
        if (this.iSessionId !== this.kSessionIdStart
            && this.iSessionId !== this.kSessionIdInvalid) {
            this.SendTerminate();
            this.iSessionId = this.kSessionIdInvalid;
        }
    }

    LongPoll.prototype.ConstructSessionId = function()
    {
        var sessionId = "session-id: "+this.iSessionId;
        return sessionId;
    }

    LongPoll.prototype.SendCreate = function()
    {
        console.log("LongPoll.SendCreate \n");
        var longPoll = this;
        var request = longPoll.NewCreateRequest();

        request.SetOnReadyStateChange(
            function SendCreateCallback() {
                console.log("readyState: %d, status: %d", request.ReadyState(), request.Status());
                if (request.ReadyState() == 4) {
                    if (request.Status() == 200) {
                        console.log("LongPoll.SendCreate success.");
                        var lines = request.ResponseText().split("\r\n");
                        var session = lines[1].split(":");
                        if (session.length == 2) {
                            var sessionVal = session[1].split(" ");
                            if ((session[0] == "session-id") && (sessionVal.length == 2)) {
                                longPoll.iSessionId = parseInt(sessionVal[1].trim());
                                longPoll.iCallbackStarted();
                                longPoll.ResponseSuccess(request);
                                return;
                            }
                        }
                    }
                    else {
                        // Covers status of 0, i.e., network issues, including request timeout.
                        longPoll.ResponseError(request);
                    }
                }
                else if (request.ReadyState() == 0) {
                    // Unable to send request for some reason.
                    longPoll.ResponseError(request);
                }
            }
        );

        request.Open("POST", "lpcreate", true);
        request.SetTimeout(this.kResponseTimeoutMs);
        request.SetRequestHeader("Content-type", "text/plain");
        request.Send();
    }

    LongPoll.prototype.SendTerminate = function()
    {
        // This should only be called when a browser tab closes.
        var longPoll = this;
        var request = longPoll.NewTerminateRequest();

        request.SetOnReadyStateChange(
            function SendTerminateCallback() {
                if (request.ReadyState() == 4) {
                    if (request.Status() == 200) {
                        /*if (longPoll.iSessionId == longPoll.kSessionIdInvalid) {
                            console.log("LongPoll.SendTerminate.SendTerminateCallback received response for invalid session: %d.", longPoll.iSessionId);
                            longPoll.ResponseError(request);
                            return;
                        }
                        */
                        longPoll.ResponseSuccess(request);
                    }
                    else {
                        // Covers status of 0, i.e., network issues.
                        longPoll.ResponseError(request);
                    }
                }
                else if (request.ReadyState() == 0) {
                    // Unable to send request for some reason.
                    longPoll.ResponseError(request);
                }
            }
        );

        request.Open("POST", "lpterminate", true);
        request.SetTimeout(this.kResponseTimeoutMs);
        var sessionId = this.ConstructSessionId();
        request.SetRequestHeader("Content-type", "text/plain");
        request.Send(sessionId+"\r\n");
    }

    LongPoll.prototype.SendPoll = function() {
        console.log("LongPoll.SendPoll\n");
        var longPoll = this;
        var request = longPoll.NewLongPollRequest();

        request.SetOnReadyStateChange(
            function LongPollCallback() {
                console.log('LongPoll.SendPoll.LongPollCallback ReadyState: %d, Status: %d', request.ReadyState(), request.Status());
                if (request.ReadyState() == 4) {
                    if (request.Status() == 200) {
                        if (longPoll.iSessionId == longPoll.kSessionIdInvalid) {
                            console.log("LongPoll.SendPoll.LongPollCallback received response for invalid session: %d.", longPoll.iSessionId);
                            longPoll.ResponseError(request);
                            return;
                        }

                        var lines = this.responseText.split("\r\n");
                        //var request = lines[0];
                        console.log("LongPoll.SendPoll.LongPollCallback responseText: " + this.responseText);

                        // Split string after request line, as the response (i.e., any JSON) may contain newlines.
                        var json = this.responseText.substring(lines[0].length+2);    // +2 to account for stripped \r\n
                        console.log('LongPoll.SendPoll.Response json:\n' + json);
                        try {
                            longPoll.ParseResponse(json);
                            longPoll.ResponseSuccess(request);
                            return;
                        }
                        catch (err) {
                            console.log("LongPoll.prototype.SendPoll.Response " + err);
                            longPoll.ResponseError(request);
                            // FIXME - as web page is no longer usable at this point (as long polls have been terminated), replace with a custom error page (that encourages user to reload, and/or also trigger a probe request that will attempt to reload the page if the user doesn't manually reload)?
                        }
                    }
                    else {
                        // Covers status of 0, i.e., network issues.
                        longPoll.ResponseError(request);
                    }
                }
                else if (this.readyState == 0) {
                    // Unable to send request for some reason.
                    longPoll.ResponseError(request);
                }
            }
        );

        request.Open("POST", "lp", true);
        request.SetTimeout(this.kResponseTimeoutMs);
        var sessionId = this.ConstructSessionId();
        console.log("sessionId: %s", sessionId);
        request.SetRequestHeader("Content-type", "text/plain");
        request.Send(sessionId+"\r\n");
    }

    LongPoll.prototype.SendUpdate = function(aString, aCallbackResponse, aCallbackError)
    {
        console.log("LongPoll.SendUpdate " + aString);
        var longPoll = this;
        var request = longPoll.NewUpdateRequest();

        request.SetOnReadyStateChange(
            function SendUpdateCallback() {
                if (request.ReadyState() == 4) {
                    if (request.Status() == 200) {
                        if (longPoll.iSessionId == longPoll.kSessionIdInvalid) {
                            console.log("LongPoll.SendUpdate.SendUpdateCallback received response for invalid session: %d.", longPoll.iSessionId);
                            longPoll.ResponseError(request);
                            return;
                        }
                        aCallbackResponse(aString, request.ResponseText());
                        longPoll.ResponseSuccess(request);
                    }
                    else {
                        // Covers status of 0, i.e., network issues.
                        aCallbackError(aString, request.ResponseText(), request.Status());
                        longPoll.ResponseError(request);
                    }
                }
                else if (request.ReadyState() == 0) {
                    // Unable to send request for some reason.
                    aCallbackError(aString, request.ResponseText(), request.Status());
                    longPoll.ResponseError(request);
                }
            }
        );

        request.Open("POST", "update", true);
        request.SetTimeout(this.kResponseTimeoutMs);
        var sessionId = this.ConstructSessionId();
        request.SetRequestHeader("Content-type", "text/plain");
        //request.setRequestHeader("Connection", "close");
        request.Send(sessionId+"\r\n"+aString+"\r\n");
    }

    LongPoll.prototype.Start = function()
    {
        this.iSessionId = this.kSessionIdStart;
        this.SendCreate();
    }

    LongPoll.prototype.Restart = function()
    {
        console.log("LongPoll.Restart this.iSessionId: " + this.iSessionId + " this.kSessionIdInvalid " + this.kSessionIdInvalid);
        this.AbortAllRequests();
        // Delay before trying to reconnect.
        setTimeout(this.Start, this.kRetryTimeoutMs);
    }

    LongPoll.prototype.ParseResponse = function(aResponse)
    {
        var parseErr = "Unable to parse JSON";
        if (aResponse == "") {
            this.iCallbackSuccess(aResponse);
        }
        else {
            try {
                var json = JSON.parse(aResponse);
                if (!json) {
                    throw parseErr;
                }
                this.iCallbackSuccess(json);
            }
            catch (err) {
                throw parseErr;
            }

        }
    }

    return {
        /**
         * Start AJAX long polling. Should be set as the onload() event by
         * client code.
         *
         * Parameter aUpdateCallback is a function taking a string as an
         * argument. Called when an update is received from server.
         * Parameter aFailureCallback is a function taking no arguments. Called
         * when long polling connection with server is lost. Allows clients to
         * provide some visibility of polling failure to clients.
         */
        StartLongPolling: function(aStartedCallback, aUpdateCallback, aFailureCallback)
        {
            if (aUpdateCallback == undefined) {
                alert("Error: WebUi.StartLongPolling(): aUpdateCallback is " + aUpdateCallback);
            }
            if (aFailureCallback == undefined) {
                alert("Error: WebUi.StartLongPolling(): aFailureCallback is " + aFailureCallback);
            }
            if (gStarted == true) {
                alert("Error: WebUi.StartLongPolling(): long polling already active");
                return;
            }
            gStarted = true;
            gLongPoll = new LongPoll(aStartedCallback, aUpdateCallback, aFailureCallback);
            gLongPoll.Start();
        },

        /**
         * Terminate AJAX long polling. Should be set as the onunload() event
         * by client code.
         */
        EndLongPolling: function()
        {
            if (gLongPoll == null) {
                alert("Error: WebUi.EndLongPolling(): StartLongPolling() must have been called prior to EndLongPolling()");
            }

            if (gStarted == false) {
                alert("Error: WebUi.EndLongPolling(): long polling is not active");
                return;
            }
            var asynchronous = false;
            gLongPoll.Terminate(asynchronous);
            gStarted = false;
        },

        RestartLongPolling: function()
        {
            if (gLongPoll == null) {
                alert("Error: WebUi.RestartLongPolling(): StartLongPolling() must have been called prior to RestartLongPolling()");
            }
            gLongPoll.Restart();
        },

        /**
         * Send data from browser to server.
         *
         * param aString:           String of a user-defined format.
         * param aCallbackResponse: Response callback function of form CallbackResponse(aStringSent, aStringReceived).
         * param aCallbackError:    Error callback function of form CallbackError(aStringSent).
         * returns:                 null.
         */
        SendUpdateToServer: function(aString, aCallbackResponse, aCallbackError)
        {
            if (gLongPoll == null) {
                alert("Error: WebUi.SendUpdateToServer(): StartLongPolling() has not been called");
                return;
            }
            if (aCallbackResponse == undefined) {
                alert("Error: WebUi.SendUpdateToServer(): aCallbackResponse is " + aCallbackResponse);
                return;
            }
            if (aCallbackError == undefined) {
                alert("Error: WebUi.SendUpdateToServer(): aCallbackError is " + aCallbackError);
                return;
            }
            gLongPoll.SendUpdate(aString, aCallbackResponse, aCallbackError);
        }
    };

}(); // WebUi
