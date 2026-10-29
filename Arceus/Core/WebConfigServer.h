#pragma once

class User;

// ============================================================================
//  WebConfigServer.h
//  ---------------------------------------------------------------------------
//  Minimal in-process HTTP server used to expose the full configuration at
//  http://localhost:4890/. Serves the embedded page from WebPanelPage.h
//  (or an external "webpanel\index.html" next to the executable, if present)
//  and translates GET/POST /config into JSON reads/writes on the live `User`
//  instance (the same state the desktop ImGui menu edits). Every successful
//  POST is persisted directly into config\<game>.usr.
//
//  Protocol:
//    GET  /                     -> dashboard page (HTML)
//    GET  /config               -> complete User state as JSON
//    POST /config               -> JSON body applied to the live User and
//                                  saved into the current game's .usr file.
//                                  Sending {"reset":true} restores defaults.
//    GET  /windows              -> visible window titles (OBS target picker)
//    GET  /capture-devices      -> video capture devices (capture card)
//    POST /action {"start":true}-> starts the app from the start menu state
//
//  The server binds only to the loopback interface and is intended for local
//  use while the application is running.
// ============================================================================

namespace WebConfigServer
{
    // Attaches the live User instance the server reads from / writes to.
    // Must be called before the page tries to fetch /config.
    void SetUser(User* user);

    // Starts the loopback HTTP listener on 127.0.0.1:<port> in a background
    // thread. Calling Start twice is harmless (the existing server keeps
    // running). Returns false if Winsock init or socket bind fails.
    bool Start(unsigned short port = 4890);

    // Stops the listener, joins the server thread and releases Winsock.
    // Safe to call from the destructor of App.
    void Stop();

    // True while the server thread is active.
    bool IsRunning();

    // Requests the host application to shut down (checked via IsExitRequested).
    void RequestExit();

    // True once RequestExit() has been called.
    bool IsExitRequested();
}
