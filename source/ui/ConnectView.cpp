#include "ConnectView.h"
#include "../core/ExitZone.h"
#include "../core/Text.h"
#include "Ui.h"
#include "../input/Input.h"
#include "../core/SoundFX.h"
#include "../core/Lang.h"
#include <wiikeyboard/keyboard.h>
#include <ogc/lwp.h>
#include <string.h>
#include <stdio.h>
#include <memory>
#include <functional>
#include <ogc/lwp_watchdog.h>


// Tab bar (drawn by renderBackground, hit-tested by update): three equal
// segments, centred
static const float TAB_W = 180, TAB_X0 = (640 - 3 * TAB_W) / 2, TAB_Y = 52, TAB_H = 30;

// USB keyboard callback — appends char to a shared buffer

// ---------------------------------------------------------------------------
// Background discovery thread
// ---------------------------------------------------------------------------
static u8  s_discoverStack[32 * 1024] DEAD_AT_EXIT;

struct DiscoverCtx {
    JellyfinClient*              client;
    std::vector<DiscoveredServer>* out;
    volatile bool                done;
};

static void* discoverWorker(void* arg) {
    DiscoverCtx* ctx = static_cast<DiscoverCtx*>(arg);
    ctx->client->discoverServers(*ctx->out);
    ctx->done = true;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Background requests (sign-in, Quick Connect)
// ---------------------------------------------------------------------------
/* Not DEAD_AT_EXIT: HOME can quit while a request is still running. */
static u8 s_jobStack[64 * 1024] __attribute__((aligned(32)));   /* mbedTLS handshake */

struct ConnectView::Job {
    std::function<void()>          work;   /* job thread */
    std::function<ConnectResult()> done;   /* main thread, once work returned */
    bool                           quiet;  /* the screen stays usable meanwhile */
    volatile bool                  running = true;
};

static void* jobWorker(void* arg) {
    ConnectView::Job* j = static_cast<ConnectView::Job*>(arg);
    j->work();
    j->running = false;
    return nullptr;
}

// ---------------------------------------------------------------
ConnectView::~ConnectView() {
    /* the threads use this view's members: let them finish first */
    if (jobThread != LWP_THREAD_NULL) LWP_JoinThread(jobThread, nullptr);
    delete job;
    if (discoverThread != LWP_THREAD_NULL) LWP_JoinThread(discoverThread, nullptr);
    delete discoverCtx;
}

bool ConnectView::needNetwork(AfterNet then) {
    if (netReady || (netReady = client.takeNetworkResult())) return true;
    client.startNetwork();
    afterNet = then;
    busy     = Busy::Network;
    return false;
}

/* Runs work() off the main thread (every request times out on its own),
 * then done() on the main thread.  Unless quiet, the screen shows `label`
 * and ignores input meanwhile. */
void ConnectView::startJob(const char* label, bool quiet, std::function<void()> work,
                           std::function<ConnectResult()> done) {
    job = new Job{ std::move(work), std::move(done), quiet };
    busyLabel = label;
    if (LWP_CreateThread(&jobThread, jobWorker, job, s_jobStack, sizeof(s_jobStack), 50) < 0) {
        jobThread = LWP_THREAD_NULL;
        jobWorker(job);                     /* no thread: run it here */
    }
    busy = quiet ? Busy::None : Busy::Job;
}

/* A finished job: its done() result, else None. */
ConnectResult ConnectView::finishJob() {
    if (!job || job->running) return ConnectResult::None;
    if (jobThread != LWP_THREAD_NULL) LWP_JoinThread(jobThread, nullptr);
    jobThread = LWP_THREAD_NULL;
    Job* j = job;
    job = nullptr;
    if (busy == Busy::Job) busy = Busy::None;
    ConnectResult r = j->done();
    delete j;
    return r;
}

void ConnectView::startLogin() {
    while (fields[0].size() > 1 && fields[0].back() == '/') fields[0].pop_back();
    serverUrl = fields[0];
    struct Out { bool ok = false; JellyfinAuth auth; std::string err; };
    auto out = std::make_shared<Out>();
    std::string url = fields[0], user = fields[1], pass = fields[2];
    bool isEditing = editing;
    JellyfinAuth exAuth = existingAuth;
    std::string origUser = origUsername;
    startJob(N_("Signing in"), false,
        [this, out, url, user, pass, isEditing, exAuth, origUser]() {
            bool sameUser = origUser.empty() || (user == origUser);
            if (isEditing && pass.empty() && !exAuth.accessToken.empty() && sameUser) {
                if (client.validateAuth(url, exAuth)) {
                    out->auth = exAuth;
                    std::string sn;
                    if (client.getServerName(url, out->auth, sn)) out->auth.serverName = sn;
                    out->ok = true;
                    return;
                }
            }
            if (pass.empty() && isEditing && !sameUser) {
                out->err = tr("Enter password for new username");
                out->ok = false;
                return;
            }
            out->ok = client.authenticate(url, user, pass, out->auth);
            if (out->ok) {
                std::string sn;
                if (client.getServerName(url, out->auth, sn)) out->auth.serverName = sn;
            } else {
                out->err = client.lastError();
            }
        },
        [this, out, user]() {
            if (!out->ok) { setStatus(out->err, true); return ConnectResult::None; }
            auth = out->auth;
            username = user;
            return ConnectResult::Success;
        });
}

/* While a slow step runs: keep the screen alive, collect its result. */
ConnectResult ConnectView::updateBusy() {
    if (busy == Busy::Network) {
        if (Input::isBackPressed()) {            /* start-up goes on in the background */
            busy = Busy::None;
            setStatus(N_("Cancelled."), true);
            return ConnectResult::None;
        }
        if (client.networkBusy()) return ConnectResult::None;
        busy = Busy::None;
        if (!client.takeNetworkResult()) { setStatus(client.lastError(), true); return ConnectResult::None; }
        netReady = true;
        if      (afterNet == AfterNet::Login)        startLogin();
        else if (afterNet == AfterNet::QuickConnect) autoQuickConnect = true;
        else                                         autoDiscover = true;
        return ConnectResult::None;
    }
    if (busy == Busy::Job) {
        if (Input::isBackPressed()) {
            SoundFX::play(SoundFX::FX::Back);
            client.abort();
            if (jobThread != LWP_THREAD_NULL) LWP_JoinThread(jobThread, nullptr);
            jobThread = LWP_THREAD_NULL;
            delete job;
            job = nullptr;
            busy = Busy::None;
            client.resetAbort();
            setStatus(N_("Cancelled."), true);
            return ConnectResult::None;
        }
    }
    return finishJob();
}

ConnectView::ConnectView(GRRLIB_texImg* btn, GRRLIB_texImg* cursor,
                         GRRLIB_ttfFont* f, JellyfinClient& c)
    : btnTex(btn), cursorTex(cursor), font(f), client(c) {
    fields[0] = "";
    fields[1] = "";
    fields[2] = "";
}

void ConnectView::setFields(const std::string& url, const std::string& uname) {
    fields[0] = url;
    // Strip trailing slashes to prevent double-slash in API paths
    while (fields[0].size() > 1 && fields[0].back() == '/')
        fields[0].pop_back();
    fields[1] = uname;
    origUsername = uname;
    serverUrl  = fields[0];
}

void ConnectView::setEditing(int field) {
    editing = true;
    focusedField = field == 1 ? Field::Username : field == 2 ? Field::Password : Field::Server;
}

void ConnectView::setStatus(const std::string& msg, bool isError) {
    statusMsg   = msg;
    statusError = isError;
    statusTimer = 180; // ~3 s at 60fps
}

// ---------------------------------------------------------------
// Main update loop
// ---------------------------------------------------------------
ConnectResult ConnectView::update(ir_t& ir) {
    // USB keyboard input (Input runs the keyboard; the on-screen keyboard,
    // when open, takes it)
    while (!kbActive) {
        const int c = Input::typedChar();
        if (!c) break;
        int fi = (int)focusedField;
        if (fi <= 2) {
            if (c == '\b') {
                if (!fields[fi].empty()) fields[fi].pop_back();
            } else if (c >= 32 && c < 127) {
                fields[fi] += (char)c;
            }
        }
    }

    // Status timer
    if (statusTimer > 0) statusTimer--;

    if (busy != Busy::None) return updateBusy();
    if (job) {                                   /* a quiet job (Quick Connect polling) */
        ConnectResult r = finishJob();
        if (r != ConnectResult::None) return r;
    }

    // B = cancel (or close VKB)
    if (Input::isBackPressed() && !kbActive) {
        // Join any in-flight discovery thread before leaving
        if (discoverThread != LWP_THREAD_NULL) {
            LWP_JoinThread(discoverThread, nullptr);
            discoverThread = LWP_THREAD_NULL;
            delete discoverCtx;
            discoverCtx = nullptr;
        }
        return ConnectResult::Cancelled;
    }

    // --- Tab switching: IR click on tab headers OR L/R buttons ---
    bool aPressed = Input::isAJustPressed();

    // Pointer on the screen: A only acts on what it points at.  No pointer:
    // the D-pad drives, A acts on the highlighted item.
    irMode = ir.valid;

    bool irTabHandled = false;
    if (ir.valid && aPressed && ir.y >= TAB_Y && ir.y <= TAB_Y + TAB_H &&
        ir.x >= TAB_X0 && ir.x < TAB_X0 + 3 * TAB_W) {
        activeTab = (Tab)(int)((ir.x - TAB_X0) / TAB_W);
        kbActive = false;
        irTabHandled = true;
    }
    if (!kbActive) {
        if (Input::isLPressed()) { activeTab = (Tab)(((int)activeTab - 1 + 3) % 3); kbActive = false; irMode = false; }
        if (Input::isRPressed()) { activeTab = (Tab)(((int)activeTab + 1) % 3);     kbActive = false; irMode = false; }
    }

    if (irTabHandled) return ConnectResult::None;

    if (activeTab == Tab::Credentials) {
        if (!kbActive) {
            // IR click: focus a field or submit
            if (ir.valid && aPressed) {
                bool irFieldHandled = false;
                // Field hit areas (from renderCredentials): y=110, y=180, y=250, button y=330
                if (ir.x >= 80 && ir.x <= 560) {
                    if      (ir.y >= 100 && ir.y <= 145) { focusedField = Field::Server;   openKeyboard(); irFieldHandled = true; irMode = true; }
                    else if (ir.y >= 170 && ir.y <= 215) { focusedField = Field::Username;  openKeyboard(); irFieldHandled = true; irMode = true; }
                    else if (ir.y >= 240 && ir.y <= 285) { focusedField = Field::Password;  openKeyboard(); irFieldHandled = true; irMode = true; }
                }
                // Connect button: centered at x=320, y=330, 200x48
                if (!irFieldHandled &&
                    ir.x >= 220 && ir.x <= 420 &&
                    ir.y >= 330 && ir.y <= 378) {
                    focusedField = Field::SubmitBtn; irFieldHandled = true; irMode = true;
                    goto doSubmit;
                }
                if (irFieldHandled) return ConnectResult::None;
                return ConnectResult::None; // IR click missed all targets
            }

            // D-pad navigation
            if (Input::isDownPressed()) {
                int f = (int)focusedField;
                focusedField = (Field)((f + 1) % 4);
                irMode = false;
            }
            if (Input::isUpPressed()) {
                int f = (int)focusedField;
                focusedField = (Field)(((f - 1) + 4) % 4);
                irMode = false;
            }
            // A button: open VKB or submit (only when not in IR mode)
            if (aPressed && !irMode) {
                if (focusedField != Field::SubmitBtn) {
                    openKeyboard();
                } else {
                    doSubmit:
                    SoundFX::play(SoundFX::FX::Start);
                    if (needNetwork(AfterNet::Login)) startLogin();
                }
            }
        } else {
            // VKB is active
            handleVKBInput(ir);
        }
    } else if (activeTab == Tab::QuickConnect) {
        // Quick Connect tab
        switch (qcState) {
            case QCState::Idle: {
                // QC button: cx=320, y=250, w=240, h=52 → x:200-440, y:250-302
                bool qcBtnHit = ir.valid &&
                                ir.x >= 200 && ir.x <= 440 &&
                                ir.y >= 250 && ir.y <= 302;
                if (qcBtnHit) irMode = true;
                if ((aPressed && (qcBtnHit || (!ir.valid && !irMode))) || autoQuickConnect) {
                    autoQuickConnect = false;
                    if (fields[0].empty()) {
                        setStatus(N_("Set the Server URL in the Credentials tab first."), true);
                        break;
                    }
                    if (!needNetwork(AfterNet::QuickConnect)) break;
                    while (fields[0].size() > 1 && fields[0].back() == '/') fields[0].pop_back();
                    serverUrl = fields[0];
                    auto ok = std::make_shared<bool>(false);
                    auto res = std::make_shared<QuickConnectResult>();
                    auto err = std::make_shared<std::string>();
                    std::string url = serverUrl;
                    startJob(N_("Contacting the server"), false,
                        [this, ok, res, err, url]() {
                            *ok = client.quickConnectInitiate(url, *res);
                            if (!*ok) *err = client.lastError();
                        },
                        [this, ok, res, err]() {
                            if (!*ok) { setStatus(*err, true); return ConnectResult::None; }
                            qcResult    = *res;
                            qcState     = QCState::Waiting;
                            qcPollTimer = 90;
                            setStatus(N_("Enter code on another device:"));
                            return ConnectResult::None;
                        });
                }
                break;
            }
            case QCState::Waiting:
                /* Poll every ~1.5 s in the background: the code stays on
                 * screen and B still cancels. */
                if (!job && --qcPollTimer <= 0) {
                    qcPollTimer = 90;
                    auto approved = std::make_shared<bool>(false);
                    auto signedIn = std::make_shared<bool>(false);
                    auto a   = std::make_shared<JellyfinAuth>();
                    auto err = std::make_shared<std::string>();
                    auto user = std::make_shared<std::string>();
                    std::string url = serverUrl, secret = qcResult.secret;
                    startJob(N_("Waiting for approval"), true,
                        [this, approved, signedIn, a, err, user, url, secret]() {
                            QuickConnectResult r;
                            if (!client.quickConnectCheck(url, secret, r) || !r.authenticated) return;
                            *approved = true;
                            *signedIn = client.quickConnectAuthenticate(url, secret, *a);
                            if (!*signedIn) { *err = client.lastError(); return; }
                            std::string sn;
                            *user = a->serverName;  /* "Name" field = user display name, before overwrite */
                            if (client.getServerName(url, *a, sn)) a->serverName = sn;
                        },
                        [this, approved, signedIn, a, err, user]() {
                            if (qcState != QCState::Waiting || !*approved) return ConnectResult::None;
                            if (!*signedIn) {
                                setStatus(*err, true);
                                qcState = QCState::Error;
                                return ConnectResult::None;
                            }
                            qcState  = QCState::Done;
                            auth     = *a;
                            username = *user;
                            return ConnectResult::Success;
                        });
                }
                if (Input::isBPressed()) { qcState = QCState::Idle; }
                break;
            case QCState::Error:
                if (aPressed && (!irMode || ir.valid)) qcState = QCState::Idle;
                break;
            default: break;
        }
    } else {
        // Discover tab
        switch (discoverState) {
            case DiscoverState::Idle: {
                bool btnHit = ir.valid &&
                              ir.x >= 200 && ir.x <= 440 &&
                              ir.y >= 240 && ir.y <= 292;
                if (btnHit) irMode = true;
                if ((aPressed && (btnHit || (!irMode && !ir.valid))) || autoDiscover) {
                    autoDiscover = false;
                    if (!needNetwork(AfterNet::Discover)) break;
                    discoveredServers.clear();
                    discoverSelected = 0;
                    discoverCtx = new DiscoverCtx{&client, &discoveredServers, false};
                    if (LWP_CreateThread(&discoverThread, discoverWorker, discoverCtx,
                                         s_discoverStack, sizeof(s_discoverStack), 64) < 0) {
                        delete discoverCtx;
                        discoverCtx = nullptr;
                        discoverThread = LWP_THREAD_NULL;
                        setStatus(N_("Failed to start scan thread."), true);
                        break;
                    }
                    discoverState = DiscoverState::Scanning;
                }
                break;
            }
            case DiscoverState::Scanning:
                if (discoverCtx && discoverCtx->done) {
                    LWP_JoinThread(discoverThread, nullptr);
                    discoverThread = LWP_THREAD_NULL;
                    delete discoverCtx;
                    discoverCtx = nullptr;
                    discoverState  = DiscoverState::Done;
                    if (discoveredServers.empty())
                        setStatus(N_("No servers found on the local network."), false);
                }
                break;
            case DiscoverState::Done: {
                int n = (int)discoveredServers.size();
                if (n == 0) {
                    // Re-scan button (same hit zone as Scan)
                    bool btnHit = ir.valid &&
                                  ir.x >= 200 && ir.x <= 440 &&
                                  ir.y >= 240 && ir.y <= 292;
                    if (btnHit) irMode = true;
                    if (aPressed && (btnHit || (!irMode && !ir.valid)))
                        discoverState = DiscoverState::Idle;
                    break;
                }
                // Navigate list — clamp to the 6 visible rows
                int visible = (n < 6) ? n : 6;
                if (Input::isDownPressed()) { discoverSelected = (discoverSelected + 1) % visible; irMode = false; }
                if (Input::isUpPressed())   { discoverSelected = (discoverSelected - 1 + visible) % visible; irMode = false; }

                bool doSelect = false;
                if (ir.valid) {
                    for (int i = 0; i < n && i < 6; i++) {
                        int rowY = 130 + i * 48;
                        if (ir.y >= rowY && ir.y <= rowY + 40 &&
                            ir.x >= 60  && ir.x <= 580) {
                            irMode = true;
                            if (aPressed) { discoverSelected = i; doSelect = true; }
                            break;
                        }
                    }
                }
                if (aPressed && !irMode) doSelect = true;

                if (doSelect && discoverSelected < n) {
                    fields[0] = discoveredServers[discoverSelected].address;
                    while (fields[0].size() > 1 && fields[0].back() == '/')
                        fields[0].pop_back();
                    serverUrl = fields[0];
                    activeTab = Tab::Credentials;
                    focusedField = Field::Username;
                    setStatus(N_("Server URL set. Enter your credentials."), false);
                }
                break;
            }
        }
    }
    return ConnectResult::None;
}

void ConnectView::openKeyboard() {
    int fi = (int)focusedField;
    if (fi <= 2) origField = fields[fi];
    kbActive = true;
    showPassword = false;
    headerNav = HeaderNav::Field;
    kb.reset();
}

void ConnectView::cancelKeyboard() {
    int fi = (int)focusedField;
    if (fi <= 2) fields[fi] = origField;
    kbActive = false;
    showPassword = false;
    headerNav = HeaderNav::Field;
    SoundFX::play(SoundFX::FX::Back);
}

// ---------------------------------------------------------------
// Virtual keyboard input
// ---------------------------------------------------------------
void ConnectView::handleVKBInput(ir_t& ir) {
    int fi = (int)focusedField;
    if (fi > 2) { kbActive = false; return; }

    const int PX = (int)((640 - kb.width()) * 0.5f) - 12, PW = (int)kb.width() + 24;
    const int PY = (int)(238 - kb.lift()) - 50;
    const bool isPw = (fi == 2);
    const int btnShowW = isPw ? 46 : 0;
    const int btnShift = isPw ? (btnShowW + 6) : 0;
    const int fieldX = PX + 100, fieldY = PY + 7, fieldW = PW - 305 - btnShift, fieldH = 32;
    const int btnLeftX = PX + PW - 200 - btnShift, btnRightX = PX + PW - 168 - btnShift;
    const int btnShowX = PX + PW - 136 - btnShift;
    const int btnClearX = PX + PW - 136, btnClearW = 46;
    const int btnCancelX = PX + PW - 86, btnCancelW = 76, btnH = 32;

    kb.setOrigin((640 - kb.width()) * 0.5f, 238 - kb.lift());
    kb.setEnterLabel(fi < 2 ? N_("Next") : N_("Done"));
    kb.setUrlKeys(fi == 0);   /* the server's address: http:// and https:// keys */
    kb.setPasswordMode(isPw, showPassword);
    kb.setTextField(fieldX, fieldY, fieldW, fieldH);
    kb.setCustomHeaderNav(true);

    int prevRow = kb.selectedRow();

    // Check header button clicks with pointer
    if (ir.valid && Input::isAJustPressed()) {
        if (ir.y >= fieldY && ir.y <= fieldY + btnH) {
            if (ir.x >= btnLeftX && ir.x <= btnLeftX + 28) {
                int c = kb.cursorPos();
                if (c < 0 || c > (int)fields[fi].size()) c = (int)fields[fi].size();
                if (c > 0) { c--; kb.setCursorPos(c); SoundFX::play(SoundFX::FX::Move); }
                headerNav = HeaderNav::Left;
                kb.setSelectedRow(-1);
                return;
            }
            if (ir.x >= btnRightX && ir.x <= btnRightX + 28) {
                int c = kb.cursorPos();
                if (c < 0 || c > (int)fields[fi].size()) c = (int)fields[fi].size();
                if (c < (int)fields[fi].size()) { c++; kb.setCursorPos(c); SoundFX::play(SoundFX::FX::Move); }
                headerNav = HeaderNav::Right;
                kb.setSelectedRow(-1);
                return;
            }
            if (isPw && ir.x >= btnShowX && ir.x <= btnShowX + btnShowW) {
                showPassword = !showPassword;
                kb.setPasswordMode(true, showPassword);
                headerNav = HeaderNav::Show;
                kb.setSelectedRow(-1);
                SoundFX::play(SoundFX::FX::Start);
                return;
            }
            if (ir.x >= btnClearX && ir.x <= btnClearX + btnClearW) {
                fields[fi].clear();
                kb.setCursorPos(0);
                headerNav = HeaderNav::Clear;
                kb.setSelectedRow(-1);
                SoundFX::play(SoundFX::FX::Backspace);
                return;
            }
            if (ir.x >= btnCancelX && ir.x <= btnCancelX + btnCancelW) {
                cancelKeyboard();
                return;
            }
            if (ir.x >= fieldX && ir.x <= fieldX + fieldW) {
                headerNav = HeaderNav::Field;
            }
        }
    }

    // Process keyboard keys / USB keyboard input
    Keyboard::Result res = kb.update(ir, fields[fi], 255);
    if (res == Keyboard::Result::Enter) {
        showPassword = false;
        if (fi < 2) { focusedField = (Field)(fi + 1); openKeyboard(); }
        else        { kbActive = false; focusedField = Field::SubmitBtn; }
        return;
    } else if (res == Keyboard::Result::Cancel) {
        cancelKeyboard();
        return;
    }

    if (isPw) showPassword = kb.isPasswordShown();

    int curRow = kb.selectedRow();

    // Check if we just transitioned into the header row from the keyboard keys
    if (prevRow >= 0 && curRow < 0) {
        int col = kb.selectedCol();
        if (col <= 5) headerNav = HeaderNav::Field;
        else if (col == 6) headerNav = HeaderNav::Left;
        else if (col == 7) headerNav = HeaderNav::Right;
        else if (col == 8) headerNav = isPw ? HeaderNav::Show : HeaderNav::Clear;
        else if (col == 9) headerNav = HeaderNav::Clear;
        else headerNav = HeaderNav::Cancel;
    }

    // D-Pad and A-button handling while in the header row
    if (curRow < 0) {
        if (Input::isDownPressed()) {
            int targetCol = 0;
            switch (headerNav) {
            case HeaderNav::Left:   targetCol = 6; break;
            case HeaderNav::Right:  targetCol = 7; break;
            case HeaderNav::Show:   targetCol = 8; break;
            case HeaderNav::Clear:  targetCol = 9; break;
            case HeaderNav::Cancel: targetCol = 10; break;
            default:
                targetCol = std::min(5, (int)fields[fi].size());
                break;
            }
            kb.setSelectedRow(0);
            kb.setSelectedCol(targetCol);
            SoundFX::play(SoundFX::FX::Move);
            return;
        }

        if (Input::isUpPressed()) {
            int targetCol = 0;
            switch (headerNav) {
            case HeaderNav::Left:   targetCol = 3; break;
            case HeaderNav::Right:  targetCol = 4; break;
            case HeaderNav::Show:   targetCol = 7; break;
            case HeaderNav::Clear:  targetCol = 8; break;
            case HeaderNav::Cancel: targetCol = 9; break;
            default:                targetCol = 5; break;
            }
            kb.setSelectedRow(Keyboard::ROWS - 1);
            kb.setSelectedCol(targetCol);
            SoundFX::play(SoundFX::FX::Move);
            return;
        }

        if (Input::isLeftPressed()) {
            switch (headerNav) {
            case HeaderNav::Field: {
                int c = kb.cursorPos();
                if (c < 0 || c > (int)fields[fi].size()) c = (int)fields[fi].size();
                if (c > 0) { c--; kb.setCursorPos(c); SoundFX::play(SoundFX::FX::Move); }
                break;
            }
            case HeaderNav::Left:
                headerNav = HeaderNav::Field;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Right:
                headerNav = HeaderNav::Left;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Show:
                headerNav = HeaderNav::Right;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Clear:
                headerNav = isPw ? HeaderNav::Show : HeaderNav::Right;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Cancel:
                headerNav = HeaderNav::Clear;
                SoundFX::play(SoundFX::FX::Move);
                break;
            }
        }

        if (Input::isRightPressed()) {
            switch (headerNav) {
            case HeaderNav::Field: {
                int c = kb.cursorPos();
                if (c < 0 || c > (int)fields[fi].size()) c = (int)fields[fi].size();
                if (c < (int)fields[fi].size()) {
                    c++; kb.setCursorPos(c);
                    SoundFX::play(SoundFX::FX::Move);
                } else {
                    headerNav = HeaderNav::Left;
                    SoundFX::play(SoundFX::FX::Move);
                }
                break;
            }
            case HeaderNav::Left:
                headerNav = HeaderNav::Right;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Right:
                headerNav = isPw ? HeaderNav::Show : HeaderNav::Clear;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Show:
                headerNav = HeaderNav::Clear;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Clear:
                headerNav = HeaderNav::Cancel;
                SoundFX::play(SoundFX::FX::Move);
                break;
            case HeaderNav::Cancel:
                break;
            }
        }

        if (Input::isAJustPressed() && !ir.valid) {
            switch (headerNav) {
            case HeaderNav::Left: {
                int c = kb.cursorPos();
                if (c < 0 || c > (int)fields[fi].size()) c = (int)fields[fi].size();
                if (c > 0) { c--; kb.setCursorPos(c); SoundFX::play(SoundFX::FX::Move); }
                break;
            }
            case HeaderNav::Right: {
                int c = kb.cursorPos();
                if (c < 0 || c > (int)fields[fi].size()) c = (int)fields[fi].size();
                if (c < (int)fields[fi].size()) { c++; kb.setCursorPos(c); SoundFX::play(SoundFX::FX::Move); }
                break;
            }
            case HeaderNav::Show:
                if (isPw) {
                    showPassword = !showPassword;
                    kb.setPasswordMode(true, showPassword);
                    SoundFX::play(SoundFX::FX::Start);
                }
                break;
            case HeaderNav::Clear:
                fields[fi].clear();
                kb.setCursorPos(0);
                SoundFX::play(SoundFX::FX::Backspace);
                break;
            case HeaderNav::Cancel:
                cancelKeyboard();
                break;
            case HeaderNav::Field:
                break;
            }
        }
    }
}

// ---------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------
static void drawField(const char* label, int x, int y, int w, const std::string& value,
                      bool focused, bool masked) {
    const Ui::Palette& p = Ui::pal();
    Ui::text(x + 4, y - 21, tr(label), 15, focused ? p.accentDark : p.textDim);
    std::string display = masked ? std::string(value.size(), '*') : value;
    if (focused) display += "_"; // caret
    // keep the end (and the caret) visible
    while (display.size() > 1 && Ui::textWidth(display.c_str(), 18) > w - 20)
        display.erase(display.begin());
    Ui::field(x - 6, y - 3, w + 12, 36, display.c_str(), 18, focused);
}

static void drawButton(int cx, int y, int w, int h, const char* label, bool focused) {
    Ui::button(cx - w / 2, y, w, h, label, 20, focused ? Ui::pulse() : 0.0f);
}

// Centred paragraph line.
static void line(int y, const char* s, int size, u32 col) {
    Ui::textCentered(320, y, s, size, col);
}

void ConnectView::renderBackground() {
    const Ui::Palette& p = Ui::pal();
    Ui::background();
    Ui::text(28, 12, editing ? tr("Edit Profile") : tr("Connect to Jellyfin"), 22, p.text);

    // Tabs: one pill split into the segments update() hit-tests
    {
        const float X0 = TAB_X0, X3 = TAB_X0 + 3 * TAB_W, Y = TAB_Y, H = TAB_H;
        const float seg[4] = { X0, X0 + TAB_W, X0 + 2 * TAB_W, X3 };
        const char* names[3] = { N_("Credentials"), N_("Quick Connect"), N_("Discover") };
        Ui::shadow(X0, Y + 1, X3 - X0, H, H * 0.5f, 4.0f, p.shadow);
        Ui::roundRect(X0, Y, X3 - X0, H, H * 0.5f, p.field, Ui::mix(p.field, p.cardBottom, 0.5f));
        Ui::roundBorder(X0, Y, X3 - X0, H, H * 0.5f, 1.5f, p.fieldBorder);
        for (int i = 0; i < 3; ++i) {
            bool on = (int)activeTab == i;
            float sx = seg[i], sw = seg[i + 1] - seg[i];
            if (on) Ui::roundRect(sx + 2, Y + 2, sw - 4, H - 4, (H - 4) * 0.5f,
                                  Ui::mix(p.accent, 0xFFFFFFFF, 0.25f), p.accentDark);
            Ui::textCentered(sx + sw * 0.5f, Y + 7, tr(names[i]), 15, on ? p.textOnAccent : p.textDim);
        }
    }

    // Slow step in progress
    if (busy != Busy::None) {
        static const char* const dots[4] = { "", ".", "..", "..." };
        char msg[64];
        snprintf(msg, sizeof(msg), "%s%s",
                 busy == Busy::Network ? tr("Connecting to the network") : tr(busyLabel),
                 dots[(ticks_to_millisecs(gettime()) / 400) % 4]);
        char wb[64];
        snprintf(wb, sizeof(wb), "%s...", tr("Connecting to the network"));
        int w = Ui::textWidth(wb, 14);
        Ui::roundRect(320 - w / 2 - 16, 420, w + 32, 26, 13, p.cardTop, p.cardBottom);
        Ui::roundBorder(320 - w / 2 - 16, 420, w + 32, 26, 13, 1.5f, p.accent);
        Ui::text(320 - w / 2, 425, msg, 14, p.accentDark);
    } else
    // Status (on two lines when the message is too wide)
    if (statusTimer > 0) {
        u32 sc = statusError ? p.danger : p.ok;
        const std::string msg = tr(statusMsg.c_str());
        int sw = Ui::textWidth(msg.c_str(), 14);
        std::string l1 = msg, l2;
        if (sw > 580) {
            // Roughly split at midpoint on a space boundary
            size_t mid = msg.size() / 2;
            size_t sp  = msg.rfind(' ', mid);
            if (sp == std::string::npos) sp = mid;
            l1 = msg.substr(0, sp);
            l2 = msg.substr(sp + 1);
        }
        int w  = Ui::textWidth(l1.c_str(), 14);
        if (!l2.empty()) { int w2 = Ui::textWidth(l2.c_str(), 14); if (w2 > w) w = w2; }
        int bh = l2.empty() ? 26 : 42;
        int by = 446 - bh;
        Ui::roundRect(320 - w / 2 - 16, by, w + 32, bh, 13, p.cardTop, p.cardBottom);
        Ui::roundBorder(320 - w / 2 - 16, by, w + 32, bh, 13, 1.5f, sc);
        Ui::textCentered(320, by + 5, l1.c_str(), 14, sc);
        if (!l2.empty()) Ui::textCentered(320, by + 21, l2.c_str(), 14, sc);
    }
}

void ConnectView::renderCredentials(ir_t& ir) {
    int fx = 80, fw = 480;
    drawField(N_("Server URL"), fx, 110, fw, fields[0],
              focusedField == Field::Server && !kbActive, false);
    drawField(N_("Username"),   fx, 180, fw, fields[1],
              focusedField == Field::Username && !kbActive, false);
    drawField(N_("Password"),   fx, 250, fw, fields[2],
              focusedField == Field::Password && !kbActive, !showPassword);

    // Submit button
    drawButton(320, 330, 200, 48, editing ? N_("Save") : N_("Connect"),
               focusedField == Field::SubmitBtn && !kbActive);

    if (kbActive) {
        renderVKB(ir);
        const Ui::Hint l[] = { { "A", N_("Type") }, { "kbdel", N_("Delete") }, { "kbspace", N_("Space") } };
        const Ui::Hint r[] = { { "kbsym", N_("Symbols") }, { "+!", focusedField == Field::Password ? N_("Done") : N_("Next") }, { "B", N_("Cancel") } };
        Ui::footer(l, 3, r, 3);
    } else {
        const Ui::Hint l[] = { { "A", N_("Select") }, { "UD", N_("Move") } };
        const Ui::Hint r[] = { { "-/+", N_("Tabs") }, { "B", busy != Busy::None ? N_("Cancel") : N_("Back") } };
        Ui::footer(l, 2, r, 2);
    }
}

void ConnectView::renderVKB(ir_t& ir) {
    // Keyboard panel (covers the lower fields while typing; the edited
    // field is shown in the panel's title row)
    kb.setOrigin((640 - kb.width()) * 0.5f, 238 - kb.lift());
    kb.setUrlKeys(focusedField == Field::Server);
    kb.setPasswordMode(focusedField == Field::Password, showPassword);
    const int PX = (int)((640 - kb.width()) * 0.5f) - 12, PW = (int)kb.width() + 24;
    const int PY = (int)(238 - kb.lift()) - 50, PH = (int)kb.height() + 62;
    GRRLIB_Rectangle(Ui::screenLeft(), 0, Ui::screenWidth(), 480, Ui::alpha(Ui::pal().dim, 0.5f), 1);
    Ui::card(PX, PY, PW, PH, 16, 0.0f);
    {
        int fi = (int)focusedField;
        const char* names[3] = { N_("Server URL"), N_("Username"), N_("Password") };
        if (fi <= 2) {
            const bool isPw = (fi == 2);
            const int btnShowW = isPw ? 46 : 0;
            const int btnShift = isPw ? (btnShowW + 6) : 0;
            const int fieldX = PX + 100, fieldY = PY + 7, fieldW = PW - 305 - btnShift, fieldH = 32;
            const int btnLeftX = PX + PW - 200 - btnShift, btnRightX = PX + PW - 168 - btnShift;
            const int btnShowX = PX + PW - 136 - btnShift;
            const int btnClearX = PX + PW - 136, btnClearW = 46;
            const int btnCancelX = PX + PW - 86, btnCancelW = 76, btnH = 32;

            std::string raw = (fi == 2 && !showPassword) ? std::string(fields[fi].size(), '*') : fields[fi];
            int cPos = kb.cursorPos();
            if (cPos < 0 || cPos > (int)raw.size()) cPos = (int)raw.size();

            const int FONT_SZ = 18;
            const float textStartX = fieldX + fieldH * 0.5f;
            const float maxTextW = fieldW - fieldH;

            int scroll = 0;
            while (cPos > scroll && Ui::textWidth(raw.substr(scroll, cPos - scroll).c_str(), FONT_SZ) > maxTextW - 20)
                scroll++;
            std::string vis = raw.substr(scroll);
            while (vis.size() > 1 && Ui::textWidth(vis.c_str(), FONT_SZ) > maxTextW)
                vis.pop_back();

            int relCaret = cPos - scroll;
            if (relCaret < 0) relCaret = 0;
            if (relCaret > (int)vis.size()) relCaret = (int)vis.size();

            Ui::text(PX + 12, PY + 14, tr(names[fi]), 14, Ui::pal().textDim);

            bool inHeader  = kb.isTextFieldFocused();
            bool selField  = inHeader && (headerNav == HeaderNav::Field);
            bool selLeft   = inHeader && (headerNav == HeaderNav::Left);
            bool selRight  = inHeader && (headerNav == HeaderNav::Right);
            bool selShow   = inHeader && (headerNav == HeaderNav::Show);
            bool selClear  = inHeader && (headerNav == HeaderNav::Clear);
            bool selCancel = inHeader && (headerNav == HeaderNav::Cancel);

            Ui::field(fieldX, fieldY, fieldW, fieldH, vis.c_str(), FONT_SZ, true);
            if (selField) {
                Ui::roundBorder(fieldX, fieldY, fieldW, fieldH, fieldH * 0.5f, 2.5f, Ui::pal().accent);
            }

            // Blinking caret vertical bar (3px wide, high contrast)
            bool caretOn = (ticks_to_millisecs(gettime()) / 400) % 2 == 0;
            if (caretOn && (!inHeader || selField)) {
                float prefixW = (relCaret > 0) ? Ui::textWidth(vis.substr(0, relCaret).c_str(), FONT_SZ) : 0.0f;
                float caretX = textStartX + prefixW;
                Ui::roundRect(caretX, fieldY + 5, 3, fieldH - 10, 1.0f, Ui::pal().accent);
            }

            // [ < ] Button
            bool hovLeft = ir.valid && ir.x >= btnLeftX && ir.x <= btnLeftX + 28 &&
                           ir.y >= fieldY && ir.y <= fieldY + btnH;
            Ui::button(btnLeftX, fieldY, 28, btnH, "<", 16, (hovLeft || selLeft) ? 1.0f : 0.0f);
            if (selLeft) Ui::roundBorder(btnLeftX, fieldY, 28, btnH, btnH * 0.5f, 2.0f, Ui::pal().accent);

            // [ > ] Button
            bool hovRight = ir.valid && ir.x >= btnRightX && ir.x <= btnRightX + 28 &&
                            ir.y >= fieldY && ir.y <= fieldY + btnH;
            Ui::button(btnRightX, fieldY, 28, btnH, ">", 16, (hovRight || selRight) ? 1.0f : 0.0f);
            if (selRight) Ui::roundBorder(btnRightX, fieldY, 28, btnH, btnH * 0.5f, 2.0f, Ui::pal().accent);

            // [ Show / Hide ] Button (password only)
            if (isPw) {
                bool hovShow = ir.valid && ir.x >= btnShowX && ir.x <= btnShowX + btnShowW &&
                               ir.y >= fieldY && ir.y <= fieldY + btnH;
                Ui::button(btnShowX, fieldY, btnShowW, btnH, showPassword ? N_("Hide") : N_("Show"), 13, (hovShow || selShow) ? 1.0f : 0.0f);
                if (selShow) Ui::roundBorder(btnShowX, fieldY, btnShowW, btnH, btnH * 0.5f, 2.0f, Ui::pal().accent);
            }

            // [ Clear ] Button
            bool hovClear = ir.valid && ir.x >= btnClearX && ir.x <= btnClearX + btnClearW &&
                            ir.y >= fieldY && ir.y <= fieldY + btnH;
            Ui::button(btnClearX, fieldY, btnClearW, btnH, N_("Clear"), 13, (hovClear || selClear) ? 1.0f : 0.0f);
            if (selClear) Ui::roundBorder(btnClearX, fieldY, btnClearW, btnH, btnH * 0.5f, 2.0f, Ui::pal().accent);

            // [ Cancel ] Button
            bool hovCancel = ir.valid && ir.x >= btnCancelX && ir.x <= btnCancelX + btnCancelW &&
                             ir.y >= fieldY && ir.y <= fieldY + btnH;
            Ui::button(btnCancelX, fieldY, btnCancelW, btnH, N_("Cancel"), 14, (hovCancel || selCancel) ? 1.0f : 0.0f);
            if (selCancel) Ui::roundBorder(btnCancelX, fieldY, btnCancelW, btnH, btnH * 0.5f, 2.0f, Ui::pal().accent);
        }
    }

    kb.render(ir);
}

void ConnectView::renderQuickConnect(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    int cx = 320;
    switch (qcState) {
        case QCState::Idle: {
            Ui::card(70, 96, 500, 108, 16, 0.0f);
            line(112, tr("Quick Connect lets you sign in by approving"), 17, p.text);
            line(136, tr("a code in the Jellyfin web interface."), 17, p.text);

            /* Show current server URL or a warning if not set */
            if (fields[0].empty()) {
                line(174, tr("Go to Credentials tab and enter the Server URL first."), 14, p.danger);
            } else {
                char sbuf[128];
                snprintf(sbuf, sizeof(sbuf), tr("Server: %s"), fields[0].c_str());
                line(174, sbuf, 14, p.textDim);
            }

            bool focused = ir.valid &&
                           ir.x >= 200 && ir.x <= 440 &&
                           ir.y >= 250 && ir.y <= 302;
            drawButton(cx, 250, 240, 52, N_("Start Quick Connect"), focused || !ir.valid);
            break;
        }

        case QCState::Waiting: {
            line(112, tr("Enter this code on your Jellyfin server:"), 17, p.text);
            // Big code display on a card
            int cw = Ui::textWidth(qcResult.code.c_str(), 52);
            Ui::card(320 - cw / 2 - 30, 148, cw + 60, 80, 18, Ui::pulse());
            Ui::textCentered(320, 160, qcResult.code.c_str(), 52, p.accentDark);
            line(250, tr("Waiting for approval..."), 17, p.textDim);
            break;
        }
        case QCState::Done:
            line(200, tr("Approved! Signing in..."), 22, p.ok);
            break;
        case QCState::Error:
            line(200, tr("Error. Press A to retry."), 20, p.danger);
            break;
    }
    const Ui::Hint l[] = { { "A", N_("Select") } };
    const Ui::Hint r[] = { { "-/+", N_("Tabs") }, { "B", qcState == QCState::Waiting ? N_("Cancel") : N_("Back") } };
    Ui::footer(l, 1, r, 2);
}

void ConnectView::renderCursor(ir_t& ir) {
    if (ir.valid && cursorTex) {
        orient_t orient;
        WPAD_Orientation(Input::pointerChan(), &orient);
        if (!Ui::mouseCursor(ir.x, ir.y)) GRRLIB_DrawImg((int)ir.x - 20, (int)ir.y - 4,
                       cursorTex, Input::pointerRoll(), 1, 1, 0xFFFFFFFF);
    }
}

void ConnectView::renderDiscover(ir_t& ir) {
    const Ui::Palette& p = Ui::pal();
    int cx = 320;
    switch (discoverState) {
        case DiscoverState::Idle:
            Ui::card(70, 96, 500, 84, 16, 0.0f);
            line(114, tr("Find Jellyfin servers on your local network."), 17, p.text);
            line(142, tr("The Wii must be on the same network as the server."), 14, p.textDim);
            {
                bool btnFocus = ir.valid &&
                                ir.x >= 200 && ir.x <= 440 &&
                                ir.y >= 240 && ir.y <= 292;
                drawButton(cx, 240, 240, 52, N_("Scan for Servers"), btnFocus || !ir.valid);
            }
            break;

        case DiscoverState::Scanning:
            line(220, tr("Scanning..."), 22, p.accentDark);
            break;

        case DiscoverState::Done: {
            int n = (int)discoveredServers.size();
            if (n == 0) {
                line(170, tr("No servers found."), 20, p.danger);
                line(200, tr("Check your network and server settings, then try again."), 14, p.textDim);
                bool btnFocus = ir.valid &&
                                ir.x >= 200 && ir.x <= 440 &&
                                ir.y >= 240 && ir.y <= 292;
                drawButton(cx, 240, 200, 48, N_("Scan Again"), btnFocus || !ir.valid);
                break;
            }
            Ui::text(28, 98, tr("Found \xe2\x80\x94 select a server:"), 15, p.textDim);
            for (int i = 0; i < n && i < 6; i++) {
                int rowY = 126 + i * 50;
                bool sel = (i == discoverSelected);
                bool irHov = ir.valid &&
                             ir.x >= 60 && ir.x <= 580 &&
                             ir.y >= rowY && ir.y <= rowY + 42;
                float f = sel ? Ui::pulse() : (irHov ? 0.55f : 0.0f);
                Ui::card(60, rowY, 520, 44, 12, f);
                Ui::text(76, rowY + 5, discoveredServers[i].name.c_str(), 16,
                         Ui::mix(p.text, p.accentDark, f));
                Ui::text(76, rowY + 25, discoveredServers[i].address.c_str(), 13, p.textDim);
            }
            break;
        }
    }
    const Ui::Hint l[] = { { "A", N_("Select") } };
    const Ui::Hint r[] = { { "-/+", N_("Tabs") }, { "B", N_("Back") } };
    Ui::footer(l, 1, r, 2);
}

void ConnectView::render(ir_t& ir) {
    renderBackground();
    if      (activeTab == Tab::Credentials)  renderCredentials(ir);
    else if (activeTab == Tab::QuickConnect) renderQuickConnect(ir);
    else                                     renderDiscover(ir);
    renderCursor(ir);
}
