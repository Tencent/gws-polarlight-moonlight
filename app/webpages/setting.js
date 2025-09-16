var gset;
var gCB = null;
function CallFunc(JSONObj) {
  if (gCB != null) {
    console.log(JSON.stringify(JSONObj, null, 3));

    gCB(JSONObj);
  }
  return "0";
}

new QWebChannel(qt.webChannelTransport, function (channel) {
  // Access the C++ API
  gset = channel.objects.gsetbridge;
  // Call a function on the C++ API
});

function SendConfig(config, callback) {
  gCB = callback;
  gset.sendMsg("config", JSON.stringify(config));
}

// MatchHost | Get
async function QuerySetting(Type = "Get") {
  return new Promise((resolve, reject) => {
    try {
      SendConfig({ Type }, function (result) {
        console.log("初始化获取到的setting", result);
        resolve(result);
      });
    } catch (err) {
      console.error("Error calling C++ function:", err);
      reject(err);
    }
  });
}

async function ApplySetting(values) {
  const applyOptions = {
    Type: "Set",
    ...values,
  };
  console.log("applyOptions", applyOptions);
  return new Promise((resolve, reject) => {
    try {
      SendConfig(applyOptions, function (result) {
        console.log("apply callback data", result);
        resolve(result);
      });
    } catch (err) {
      console.error("Error calling C++ function:", err);
      reject(err);
    }
  });
}

function OpenLogDirectory() {
  gset.openLogDir();
}

function UploadLog() {
  gset.uploadLog();
}

function CloseWindow() {
  gset.Close();
}

function openUrl(url) {
  gset.openUrlInExternalBrowser(url);
}

function getVersion() {
  return gset.getVersion();
}

function getLicenseData() {
  return [
    {
      componentProject: "Modified Sunshine",
      roleInOurProduct: "Host-side capture/encode/stream",
      primaryLicence: "GPL-3.0-only",
      url: "https://placeholder.url/sunshine",
    },
    {
      componentProject: "Halo",
      roleInOurProduct:
        "Stand-alone REST/HTTP control service (split from Sunshine)",
      primaryLicence: "GPL-3.0-only",
      url: "https://placeholder.url/halo",
    },
    {
      componentProject: "HBridge",
      roleInOurProduct: "XLang IPC bridge executable",
      primaryLicence: "GPL-3.0-only",
      url: "https://placeholder.url/hbridge",
    },
    {
      componentProject: "Gleam",
      roleInOurProduct: "Cross-platform streaming client",
      primaryLicence: "GPL-3.0-only",
      url: "https://placeholder.url/gleam",
    },
    {
      componentProject: "moonlight‑common‑c",
      roleInOurProduct: "NVIDIA GameStream protocol core",
      primaryLicence: "GPL-3.0-only",
      url: "https://github.com/moonlight-stream/moonlight-common-c",
    },
    {
      componentProject: "XLang",
      roleInOurProduct: "Host‑side scripting / automation	",
      primaryLicence: "Apache‑2.0	",
      url: "https://github.com/xlang-foundation/xlang",
    },
    {
      componentProject: "Qt 5/6",
      roleInOurProduct: "Gleam GUI & cross‑platform abstraction	",
      primaryLicence: "Gleam GUI & cross‑platform abstraction	",
      url: "https://www.qt.io/",
    },
    {
      componentProject: "FFmpeg",
      roleInOurProduct: "Audio / video codecs &amp; muxers",
      primaryLicence: "LGPL-2.1-or-later or GPL-3.0 (build choice)",
      url: "https://ffmpeg.org",
    },
    {
      componentProject: "OpenSSL",
      roleInOurProduct: "TLS / cryptography",
      primaryLicence: "Dual Apache－2.0 / OpenSSL",
      url: "https://www.openssl.org",
    },
    {
      componentProject: "cURL / libcurl",
      roleInOurProduct: "HTTP(S) client",
      primaryLicence: "curl licence (MIT style)",
      url: "https://curl.se",
    },
    {
      componentProject: "libopus",
      roleInOurProduct: "Opus audio codec",
      primaryLicence: "BSD-like",
      url: "https://opus-codec.org",
    },
    {
      componentProject: "miniUPnPc",
      roleInOurProduct: "UPnP/NAT-PMP",
      primaryLicence: "BSD‑3‑Clause",
      url: "https://miniupnp.tuxfamily.org",
    },
    {
      componentProject: "nv‑codec‑headers",
      roleInOurProduct: "NVENC / NVDEC SDK headers",
      primaryLicence: "MIT",
      url: "https://github.com/FFmpeg/nv-codec-headers",
    },
    {
      componentProject: "NVFBC headers",
      roleInOurProduct: "NVIDIA Frame Buffer Capture API",
      primaryLicence: "NVIDIA custom permissive",
      url: "https://placeholder.url/nvfbc",
    },
    {
      componentProject: "glad",
      roleInOurProduct: "OpenGL / GLES loader",
      primaryLicence: "MIT",
      url: "https://glad.dav1d.de",
    },
    {
      componentProject: "Simple‑Web‑Server",
      roleInOurProduct: "Embedded HTTP server (pairing UI)",
      primaryLicence: "MIT",
      url: "https://gitlab.com/eidheim/Simple-Web-Server",
    },
    {
      componentProject: "TPCircularBuffer",
      roleInOurProduct: "Lock-free ring buffer (audio)",
      primaryLicence: "BSD",
      url: "https://github.com/michaeltyson/TPCircularBuffer",
    },
    {
      componentProject: "tray",
      roleInOurProduct: "System tray helper",
      primaryLicence: "MIT",
      url: "https://github.com/cristianbuse/tray",
    },
    {
      componentProject: "ViGEmClient (Windows)",
      roleInOurProduct: "Virtual game-pad device",
      primaryLicence: "MIT",
      url: "https://github.com/ViGEm/ViGEmClient",
    },
    {
      componentProject: "wayland-protocols",
      roleInOurProduct: "Extra Wayland protocol XMLs",
      primaryLicence: "MIT",
      url: "https://gitlab.freedesktop.org/wayland/wayland-protocols",
    },
    {
      componentProject: "nanors",
      roleInOurProduct: "Tiny C++ thread helpers",
      primaryLicence: "MIT",
      url: "https://github.com/mjansson/nanors",
    },
    {
      componentProject: "AntiHooking",
      roleInOurProduct: "Windows anti-tamper shim (Gleam)",
      primaryLicence: "MIT",
      url: "https://placeholder.url/antihooking",
    },
    {
      componentProject: "h264bitstream",
      roleInOurProduct: "H.264 SPS/PPS parser (Gleam)",
      primaryLicence: "LGPL-2.1",
      url: "https://github.com/georgmartius/h264bitstream",
    },
    {
      componentProject: "qmdnsengine",
      roleInOurProduct: "mDNS / Bonjour discovery (Gleam)",
      primaryLicence: "LGPL-2.1",
      url: "https://github.com/RaisinTen/qmdnsengine",
    },
    {
      componentProject: "soundio",
      roleInOurProduct: "Cross-platform audio I/O (Gleam)",
      primaryLicence: "ISC",
      url: "https://github.com/andrewrk/libsoundio",
    },
    {
      componentProject:
        "Other utility sub‑modules(googletest,SDL_GameControllerDB, etc.)",
      roleInOurProduct: "Build helpers, controller maps",
      primaryLicence: "MIT / BSD / Public Domain",
      url: "",
      urlDesc: "see Sunshine &amp; Gleam repos",
    },
  ];
}
