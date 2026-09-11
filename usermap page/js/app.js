(function () {
  "use strict";

  /**
   * Hub + path usermap beats.
   * Step-1: Login → Main
   * Step-2: INPUT-LIST (datasheet → load → fill)
   * Step-3: OUTPUT folder
   * Step-4: Layout setup mode (patterflowlayouts)
   * Step-5: FILE EXPORT OPTIONS
   * Step-6: START
   * Step-7: Finishing (congrats + OUTPUT files)
   */
  const DATASHEET_URL = "https://patterflowdatasheet.netlify.app/";
  const LAYOUTS_URL = "https://patterflowlayouts.netlify.app/";

  /** Formats selected in the demo export step — all options for every user */
  const NEEDED_EXPORTS = [
    { checkId: "enableTif", groupId: "tifOptionsGroup" },
    { checkId: "enableEps", groupId: "epsOptionsGroup" },
    { checkId: "enableJpg", groupId: "jpgOptionsGroup" },
    { checkId: "enablePdf", groupId: "pdfOptionsGroup" },
  ];

  const SAMPLE_ROWS = [
    {
      no: "01",
      name: "JERSEY-A",
      number: "10",
      size: "M",
      slv: "F",
      pant: "XL",
      comments: "",
    },
    {
      no: "02",
      name: "JERSEY-B",
      number: "12",
      size: "L",
      slv: "F",
      pant: "2XL",
      comments: "",
    },
    {
      no: "03",
      name: "JERSEY-C",
      number: "08",
      size: "XL",
      slv: "H",
      pant: "XS",
      comments: "no-sponsor",
    },
  ];

  const BEATS = [
    {
      id: "idle",
      stepNum: { en: "Step-1", bn: "ধাপ-১" },
      title: { en: "Login", bn: "লগইন" },
      hint: { en: "Press Next step", bn: "Next step চাপুন" },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voice: {
        en: "Welcome to PatternFlow AI. Press Next step to begin.",
        bn: "স্বাগতম PatternFlow AI-তে। শুরু করতে Next step চাপুন।",
      },
      face: "login",
      loggedIn: false,
      tour: null,
    },
    {
      id: "main",
      stepNum: { en: "Step-1", bn: "ধাপ-১" },
      title: { en: "Main panel", bn: "মেইন প্যানেল" },
      hint: {
        en: "This is your control panel",
        bn: "এটা আপনার কন্ট্রোল প্যানেল",
      },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voiceLogin: {
        en: "Enter your username and password, then press Login.",
        bn: "আপনার ইউজারনেম আর পাসওয়ার্ড দিন। তারপর Login চাপুন।",
      },
      voice: {
        en: "This is your main panel. All work starts from here.",
        bn: "এটা আপনার মেইন প্যানেল। এখান থেকেই পুরো কাজ চলবে।",
      },
      face: "main",
      loggedIn: true,
      runLoginThenMain: true,
      tour: { clearTable: true },
    },
    {
      id: "input-click",
      stepNum: { en: "Step-2", bn: "ধাপ-২" },
      title: { en: "INPUT-LIST", bn: "INPUT-LIST" },
      hint: {
        en: "Build your list first, then load it here",
        bn: "আগে লিস্ট বানান, তারপর এখানে লোড করুন",
      },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voice: {
        en: "First build your jersey list. Tap Build your list, then load it with INPUT-LIST.",
        bn: "আগে জার্সির লিস্ট বানান। Build your list চাপুন। তারপর INPUT-LIST দিয়ে লোড করুন।",
      },
      face: "main",
      loggedIn: true,
      tour: {
        spotlight: "btnLoadJson",
        clearTable: true,
        extra: "datasheet",
      },
    },
    {
      id: "input-fill",
      stepNum: { en: "Step-2", bn: "ধাপ-২" },
      title: { en: "List loaded", bn: "লিস্ট লোড হয়েছে" },
      hint: {
        en: "Rows appear in the table",
        bn: "টেবিলে রো দেখা যাচ্ছে",
      },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voice: {
        en: "Your list is now in the table. You can see all jersey details here.",
        bn: "দেখুন — আপনার লিস্ট টেবিলে চলে এসেছে। সব তথ্য এখানে দেখা যাবে।",
      },
      face: "main",
      loggedIn: true,
      tour: {
        fillSample: true,
        markInputDone: true,
        animateFill: true,
      },
    },
    {
      id: "output-select",
      stepNum: { en: "Step-3", bn: "ধাপ-৩" },
      title: { en: "OUTPUT", bn: "OUTPUT" },
      hint: {
        en: "Select the folder where files will save",
        bn: "যে ফোল্ডারে ফাইল সেভ হবে, সেটা বেছে নিন",
      },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voice: {
        en: "Now press OUTPUT and choose the folder where your files will be saved.",
        bn: "এবার OUTPUT চাপুন। যে ফোল্ডারে ফাইল সেভ হবে, সেটা সিলেক্ট করুন।",
      },
      face: "main",
      loggedIn: true,
      tour: {
        fillSample: true,
        markInputDone: true,
        spotlight: "btnSelectFolder",
        markOutputDone: true,
        animateOutput: true,
      },
    },
    {
      id: "path-hub",
      stepNum: { en: "Step-4", bn: "ধাপ-৪" },
      title: { en: "Layout setup", bn: "লেআউট সেটআপ" },
      hint: {
        en: "See which setup fits your jersey",
        bn: "কোন সেটআপ আপনার জার্সির সাথে মিলবে দেখুন",
      },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voice: {
        en: "Open View layouts to see how each path sets up jersey. Then choose Flow-1, Flow-2, Custom-Flow, or Nesting.",
        bn: "View layouts চাপলে দেখবেন কোন পাথে জার্সি কীভাবে সেটআপ হয়। তারপর Flow-1, Flow-2, Custom-Flow বা Nesting বেছে নিন।",
      },
      face: "main",
      loggedIn: true,
      tour: {
        fillSample: true,
        markInputDone: true,
        markOutputDone: true,
        extra: "layouts",
        spotlightFlows: true,
        resetExport: true,
      },
    },
    {
      id: "export-options",
      stepNum: { en: "Step-5", bn: "ধাপ-৫" },
      title: { en: "FILE EXPORT", bn: "FILE EXPORT" },
      hint: {
        en: "Select the file types you need",
        bn: "প্রয়োজনীয় ফাইল টাইপ সিলেক্ট করুন",
      },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voice: {
        en: "In File Export Options, select the file types you need. You can choose TIF, EPS, JPG, and PDF.",
        bn: "FILE EXPORT OPTIONS থেকে যে ফাইল লাগবে সিলেক্ট করুন। TIF, EPS, JPG, PDF — সব নিতে পারেন।",
      },
      face: "main",
      loggedIn: true,
      tour: {
        fillSample: true,
        markInputDone: true,
        markOutputDone: true,
        animateExport: true,
      },
    },
    {
      id: "start-run",
      stepNum: { en: "Step-6", bn: "ধাপ-৬" },
      title: { en: "START", bn: "START" },
      hint: {
        en: "Click START to run the process",
        bn: "কাজ চালাতে START চাপুন",
      },
      nextLabel: { en: "Next step", bn: "পরের ধাপ" },
      voice: {
        en: "Everything is ready. Press START to begin the process.",
        bn: "সব রেডি। এখন START চাপুন — কাজ শুরু হবে।",
      },
      face: "main",
      loggedIn: true,
      tour: {
        fillSample: true,
        markInputDone: true,
        markOutputDone: true,
        applyExport: true,
        animateStart: true,
      },
    },
    {
      id: "finish",
      stepNum: { en: "Step-7", bn: "ধাপ-৭" },
      title: { en: "Finishing", bn: "Finishing" },
      hint: {
        en: "Your files are ready in the OUTPUT folder",
        bn: "Your files are ready in the OUTPUT folder",
      },
      nextLabel: { en: "Done", bn: "শেষ" },
      voice: {
        en: "Congratulations! Your process is complete. Check the OUTPUT folder — your files are ready there.",
        bn: "অভিনন্দন! আপনার কাজ শেষ হয়েছে। OUTPUT ফোল্ডারে দেখুন — ফাইলগুলো সেখানে রেডি আছে।",
      },
      face: "main",
      loggedIn: true,
      tour: {
        fillSample: true,
        markInputDone: true,
        markOutputDone: true,
        applyExport: true,
        keepStartDone: true,
        panelExit: true,
      },
    },
  ];

  const UI = {
    back: { en: "Back", bn: "পেছনে" },
    next: { en: "Next step", bn: "পরের ধাপ" },
    done: { en: "Done", bn: "শেষ" },
    loginMidHint: {
      en: "Username, password & Login",
      bn: "ইউজারনেম, পাসওয়ার্ড ও Login",
    },
    loginMidTitle: { en: "Login", bn: "লগইন" },
    datasheetNote: {
      en: "Build your jersey list on PatternFlow Data Sheet, then Save JSON and load it with INPUT-LIST.",
      bn: "PatternFlow Data Sheet-এ জার্সি লিস্ট বানান। Save JSON করে INPUT-LIST দিয়ে লোড করুন।",
    },
    datasheetBtn: { en: "Build your list", bn: "লিস্ট বানান" },
    layoutsNote: {
      en: "Each path sets up jersey differently. Open the layout guide, then pick the matching path in the panel.",
      bn: "প্রতিটি পাথে জার্সি আলাদাভাবে সেটআপ হয়। লেআউট গাইড দেখে প্যানেলে মিলিয়ে পাথ বেছে নিন।",
    },
    layoutsBtn: { en: "View layouts", bn: "লেআউট দেখুন" },
    finishNote: {
      en: "PatternFlow AI finished processing. Collect your exported files from the OUTPUT folder you selected.",
      bn: "PatternFlow AI finished processing. Collect your exported files from the OUTPUT folder you selected.",
    },
    afterStory: {
      en: "Common path done — Flow tours next.",
      bn: "সাধারণ ধাপ শেষ — এরপর Flow ট্যুর আসবে।",
    },
    footer: {
      en: "PatternFlow AI · Extension usermap",
      bn: "PatternFlow AI · এক্সটেনশন ইউজারম্যাপ",
    },
  };

  const LANG_KEY = "pf_usermap_lang";
  const USERNAME = "Username";
  const PASSWORD = "••••••";
  const SPIN_MS = 1150;

  let lang = "en";
  let voiceEnabled = true;
  var voiceAudio = null;
  var voicePlaying = false;
  var pendingGo = null;
  var applyInFlight = 0;

  const VOICE_KEYS = {
    idle: "idle",
    main: "main",
    "input-click": "input-click",
    "input-fill": "input-fill",
    "output-select": "output",
    "path-hub": "layout",
    "export-options": "export",
    "start-run": "start",
    finish: "finish",
  };

  /** Left-side story/UI copy stays English in both languages. Voice uses `lang`. */
  function t(map) {
    if (!map) return "";
    if (typeof map === "string") return map;
    return map.en || "";
  }

  function flushPendingGo() {
    if (pendingGo == null || spinning || finishAnimating || voicePlaying) return;
    var delta = pendingGo;
    pendingGo = null;
    go(delta);
  }

  function markVoiceEnded() {
    voicePlaying = false;
    voiceAudio = null;
    // Don't jump steps while a beat animation is still running
    if (applyInFlight > 0) return;
    flushPendingGo();
  }

  function stopVoice() {
    if (!voiceAudio) {
      voicePlaying = false;
      return;
    }
    try {
      voiceAudio.onended = null;
      voiceAudio.onerror = null;
      voiceAudio.pause();
      voiceAudio.removeAttribute("src");
      voiceAudio.load();
    } catch (e) {}
    voiceAudio = null;
    voicePlaying = false;
  }

  function voiceKeyForBeat(beat, which) {
    if (which === "login") return "login";
    if (!beat) return null;
    return VOICE_KEYS[beat.id] || null;
  }

  function speakBeat(beat, which, token) {
    if (!voiceEnabled || !beat) return;
    var key = voiceKeyForBeat(beat, which);
    if (!key) return;

    stopVoice();

    var url = "./assets/voice/" + lang + "-" + key + ".mp3";
    var audio = new Audio(url);
    voiceAudio = audio;
    voicePlaying = true;

    var finishIfCurrent = function () {
      if (voiceAudio !== audio) return;
      markVoiceEnded();
    };

    audio.addEventListener("ended", finishIfCurrent);
    audio.addEventListener("error", function () {
      console.warn("Voice file missing or unreadable:", url);
      finishIfCurrent();
    });

    var playNow = function () {
      if (token != null && token !== sequenceToken) {
        if (voiceAudio === audio) {
          stopVoice();
        }
        return;
      }
      if (voiceAudio !== audio) return;
      var playPromise = audio.play();
      if (playPromise && typeof playPromise.catch === "function") {
        playPromise.catch(function (err) {
          console.warn("Voice play failed:", url, err);
          finishIfCurrent();
        });
      }
    };

    setTimeout(playNow, which === "login" ? 80 : 180);
  }

  function syncLangUI() {
    document.documentElement.lang = lang === "bn" ? "bn" : "en";
    document.querySelectorAll(".lang-switch__btn").forEach(function (btn) {
      btn.classList.toggle("is-active", btn.getAttribute("data-lang") === lang);
    });
    var after = document.getElementById("afterStoryText");
    var foot = document.getElementById("footerLine1");
    if (after) after.textContent = t(UI.afterStory);
    if (foot) foot.textContent = t(UI.footer);
    if (btnPrev) btnPrev.textContent = t(UI.back);
  }

  function setLanguage(next, options) {
    var opts = options || {};
    pendingGo = null;
    stopVoice();
    lang = next === "bn" ? "bn" : "en";
    try {
      localStorage.setItem(LANG_KEY, lang);
    } catch (e) {}
    syncLangUI();
    if (!opts.skipRefresh) {
      applyBeat(currentBeat, { skipSequence: true, speak: opts.speak !== false });
    }
  }

  function openLangModal() {
    var modal = document.getElementById("langModal");
    if (!modal) return;
    modal.hidden = false;
    document.body.classList.add("lang-lock");
  }

  function closeLangModal() {
    var modal = document.getElementById("langModal");
    if (!modal) return;
    modal.hidden = true;
    document.body.classList.remove("lang-lock");
  }

  function initLanguage() {
    var saved = null;
    try {
      saved = localStorage.getItem(LANG_KEY);
    } catch (e) {}
    if (saved === "bn" || saved === "en") {
      setLanguage(saved, { skipRefresh: true, speak: false });
      closeLangModal();
      return false;
    }
    openLangModal();
    return true;
  }

  const floatEl = document.getElementById("panelFloat");
  const loginPanel = document.getElementById("viewLogin");
  const mainPanel = document.getElementById("viewApp");
  const appBody = document.getElementById("pfAppBody");
  const storyEl = document.getElementById("story");
  const finishStage = document.getElementById("finishStage");
  const finishStageText = document.getElementById("finishStageText");
  const btnFinishBack = document.getElementById("btnFinishBack");
  const copy = document.querySelector(".story__copy");
  const storyStepsEl = document.getElementById("storySteps");
  const titleEl = document.getElementById("storyTitle");
  const hintEl = document.getElementById("storyHint");
  const storyExtra = document.getElementById("storyExtra");
  const progressFill = document.getElementById("storyProgressFill");
  const userInput = document.getElementById("mockUsername");
  const passInput = document.getElementById("mockPassword");
  const loginBtn = document.getElementById("mockLoginBtn");
  const btnPrev = document.getElementById("btnPrevStep");
  const btnNext = document.getElementById("btnNextStep");
  const tableBody = document.getElementById("mockTableBody");
  const btnLoadJson = document.getElementById("btnLoadJson");
  const btnSelectFolder = document.getElementById("btnSelectFolder");
  const btnStart = document.getElementById("btnStart");
  const logBox = document.getElementById("log-box");

  /** One tab per unique Step-N label → first beat of that step */
  const STEP_TABS = (function () {
    var tabs = [];
    var seen = {};
    BEATS.forEach(function (beat, i) {
      var key = (beat.stepNum && beat.stepNum.en) || "Step-" + (i + 1);
      if (seen[key]) return;
      seen[key] = true;
      tabs.push({ key: key, label: key, beatIndex: i });
    });
    return tabs;
  })();

  let currentBeat = 0;
  let sequenceToken = 0;
  let spinning = false;
  let finishAnimating = false;
  let yAngle = 0;
  const FINISH_EXIT_MS = 980;
  const FINISH_ENTER_MS = 880;

  function clamp(n, min, max) {
    return Math.max(min, Math.min(max, n));
  }

  function wait(ms) {
    return new Promise(function (resolve) {
      setTimeout(resolve, ms);
    });
  }

  const DESIGN_W = 340;
  const DESIGN_H = 750;
  const TARGET_W = 340;
  const TARGET_H = 750;

  function syncPanelScale() {
    var root = document.documentElement;
    root.style.setProperty("--panel-design-w", DESIGN_W + "px");
    root.style.setProperty("--panel-design-h", DESIGN_H + "px");
    root.style.setProperty("--panel-h", TARGET_H + "px");
    root.style.setProperty("--panel-w", TARGET_W + "px");
    root.style.setProperty("--panel-scale", "1");
  }

  function emptyTableHtml(count) {
    var html = "";
    for (var i = 1; i <= count; i++) {
      var no = i < 10 ? "0" + i : String(i);
      html +=
        '<tr class="data-row editable">' +
        '<td class="row-number">' +
        no +
        "</td><td></td><td></td><td></td><td></td><td></td><td></td>" +
        "</tr>";
    }
    return html;
  }

  function sampleRowHtml(row, arrive) {
    return (
      '<tr class="data-row editable is-filled' +
      (arrive ? " is-arrive" : "") +
      '">' +
      '<td class="row-number">' +
      row.no +
      "</td>" +
      "<td>" +
      row.name +
      "</td>" +
      "<td>" +
      row.number +
      "</td>" +
      "<td>" +
      row.size +
      "</td>" +
      "<td>" +
      row.slv +
      "</td>" +
      "<td>" +
      row.pant +
      "</td>" +
      "<td>" +
      row.comments +
      "</td>" +
      "</tr>"
    );
  }

  function renderEmptyTable() {
    if (!tableBody) return;
    tableBody.innerHTML = emptyTableHtml(20);
    tableBody.dataset.mode = "empty";
  }

  function renderSampleInstant() {
    if (!tableBody) return;
    var html = "";
    SAMPLE_ROWS.forEach(function (row) {
      html += sampleRowHtml(row, false);
    });
    html += emptyTableHtml(17);
    tableBody.innerHTML = html;
    tableBody.dataset.mode = "sample";
  }

  async function playFillSequence(token) {
    if (!tableBody) return;
    setNavLocked(true);
    tableBody.innerHTML = emptyTableHtml(20);
    tableBody.dataset.mode = "filling";

    for (var i = 0; i < SAMPLE_ROWS.length; i++) {
      if (token !== sequenceToken) return;
      var rows = tableBody.querySelectorAll("tr");
      if (rows[i]) {
        rows[i].outerHTML = sampleRowHtml(SAMPLE_ROWS[i], true);
      }
      await wait(220);
    }

    tableBody.dataset.mode = "sample";
    setNavLocked(false);
    updateNav(currentBeat, BEATS[currentBeat]);
  }

  function clearTourEffects() {
    document.querySelectorAll(".is-spotlight").forEach(function (el) {
      el.classList.remove("is-spotlight");
    });
    document.querySelectorAll("#data-table th.is-col-glow").forEach(function (el) {
      el.classList.remove("is-col-glow");
    });
    document.querySelectorAll(".flow-btn.active").forEach(function (el) {
      el.classList.remove("active");
    });
    if (btnLoadJson) btnLoadJson.classList.remove("completed");
    if (btnSelectFolder) btnSelectFolder.classList.remove("completed");
    if (btnStart) {
      btnStart.classList.remove("is-spotlight", "is-started", "nesting-busy");
      btnStart.textContent = "START";
    }
  }

  async function animateStartClick(token) {
    setNavLocked(true);
    if (btnStart) {
      btnStart.classList.add("is-spotlight");
      try {
        btnStart.scrollIntoView({ block: "nearest", behavior: "smooth" });
      } catch (e) {}
    }
    await wait(480);
    if (token !== sequenceToken) return;
    if (btnStart) {
      btnStart.classList.remove("is-spotlight");
      btnStart.classList.add("is-started", "nesting-busy");
      btnStart.textContent = "STARTING...";
    }
    setLogLine("[demo] START clicked — process running");
    await wait(520);
    if (token !== sequenceToken) return;
    if (btnStart) {
      btnStart.classList.remove("nesting-busy");
      btnStart.classList.add("is-started");
      btnStart.textContent = "START";
    }
    setLogLine("[demo] Process started");
    setNavLocked(false);
    updateNav(currentBeat, BEATS[currentBeat]);
  }

  function resetExportOptions() {
    ["enableTif", "enableEps", "enableJpg", "enablePdf"].forEach(function (id) {
      var el = document.getElementById(id);
      if (el) el.checked = false;
    });
    document.querySelectorAll("#saveSettings .format-option-group").forEach(function (g) {
      g.classList.remove("active", "is-spotlight");
    });
    var save = document.getElementById("saveSettings");
    if (save) save.classList.remove("is-spotlight");
  }

  function applyNeededExports() {
    NEEDED_EXPORTS.forEach(function (item) {
      var check = document.getElementById(item.checkId);
      var group = document.getElementById(item.groupId);
      if (check) check.checked = true;
      if (group) group.classList.add("active");
    });
  }

  async function animateExportSelect(token) {
    setNavLocked(true);
    resetExportOptions();
    var save = document.getElementById("saveSettings");
    if (save) {
      save.classList.add("is-spotlight");
      try {
        save.scrollIntoView({ block: "nearest", behavior: "smooth" });
      } catch (e) {}
    }
    await wait(420);
    if (token !== sequenceToken) return;

    for (var i = 0; i < NEEDED_EXPORTS.length; i++) {
      if (token !== sequenceToken) return;
      var item = NEEDED_EXPORTS[i];
      var check = document.getElementById(item.checkId);
      var group = document.getElementById(item.groupId);
      if (check) check.checked = true;
      if (group) group.classList.add("active", "is-spotlight");
      await wait(400);
      if (token !== sequenceToken) return;
      if (group) group.classList.remove("is-spotlight");
    }

    if (save) save.classList.remove("is-spotlight");
    setLogLine("[demo] Export selected: TIF + EPS + JPG + PDF");
    setNavLocked(false);
    updateNav(currentBeat, BEATS[currentBeat]);
  }

  function setLogLine(text) {
    if (!logBox) return;
    var first = logBox.querySelector("div");
    if (first) first.textContent = text;
  }

  function renderDatasheetExtra() {
    if (!storyExtra) return;
    storyExtra.hidden = false;
    storyExtra.innerHTML =
      '<p class="datasheet-note">' +
      t(UI.datasheetNote) +
      "</p>" +
      '<a class="btn btn--datasheet" href="' +
      DATASHEET_URL +
      '" target="_blank" rel="noopener noreferrer">' +
      t(UI.datasheetBtn) +
      "</a>";
  }

  function renderLayoutsExtra() {
    if (!storyExtra) return;
    storyExtra.hidden = false;
    storyExtra.innerHTML =
      '<p class="datasheet-note">' +
      t(UI.layoutsNote) +
      "</p>" +
      '<a class="btn btn--layouts" href="' +
      LAYOUTS_URL +
      '" target="_blank" rel="noopener noreferrer">' +
      t(UI.layoutsBtn) +
      "</a>";
  }

  function hideExtra() {
    if (!storyExtra) return;
    storyExtra.hidden = true;
    storyExtra.innerHTML = "";
  }

  function syncFinishStageCopy() {
    if (finishStageText) finishStageText.textContent = t(UI.finishNote);
  }

  function clearPanelMotionStyles() {
    if (!floatEl) return;
    floatEl.style.transform = "";
    floatEl.style.transition = "";
    floatEl.style.filter = "";
    floatEl.style.opacity = "";
    floatEl.style.animation = "";
    floatEl.style.visibility = "";
  }

  function setFinishBackEnabled(enabled) {
    if (btnFinishBack) btnFinishBack.disabled = !enabled;
  }

  function showFinishModeInstant() {
    syncFinishStageCopy();
    if (storyEl) storyEl.classList.add("is-finish-mode");
    document.body.classList.add("is-finish-lock");
    if (finishStage) {
      finishStage.hidden = false;
      finishStage.classList.remove("is-out");
      finishStage.classList.add("is-in");
    }
    if (floatEl) {
      clearPanelMotionStyles();
      floatEl.classList.remove("is-leaving", "is-entering", "is-spinning");
      floatEl.classList.add("is-away");
    }
    setFinishBackEnabled(true);
  }

  function hideFinishModeInstant() {
    if (storyEl) storyEl.classList.remove("is-finish-mode");
    document.body.classList.remove("is-finish-lock");
    if (finishStage) {
      finishStage.classList.remove("is-in", "is-out");
      finishStage.hidden = true;
    }
    if (floatEl) {
      floatEl.classList.remove("is-leaving", "is-away", "is-entering");
      clearPanelMotionStyles();
      setFlatTransform(0);
    }
    setFinishBackEnabled(false);
  }

  async function enterFinishMode(token) {
    finishAnimating = true;
    setNavLocked(true);
    setFinishBackEnabled(false);
    syncFinishStageCopy();

    if (storyEl) storyEl.classList.add("is-finish-mode");
    document.body.classList.add("is-finish-lock");
    if (finishStage) {
      finishStage.hidden = false;
      finishStage.classList.remove("is-in", "is-out");
    }

    if (floatEl) {
      clearPanelMotionStyles();
      floatEl.classList.remove("is-away", "is-entering", "is-spinning");
      void floatEl.offsetWidth;
      floatEl.classList.add("is-leaving");
    }

    await wait(FINISH_EXIT_MS);
    if (token !== sequenceToken) {
      finishAnimating = false;
      return;
    }

    if (floatEl) {
      floatEl.classList.remove("is-leaving");
      floatEl.classList.add("is-away");
    }
    if (finishStage) {
      void finishStage.offsetWidth;
      finishStage.classList.add("is-in");
    }

    await wait(FINISH_ENTER_MS);
    if (token !== sequenceToken) {
      finishAnimating = false;
      return;
    }

    finishAnimating = false;
    setNavLocked(false);
    setFinishBackEnabled(true);
    updateNav(currentBeat, BEATS[currentBeat]);
  }

  async function exitFinishMode(token) {
    finishAnimating = true;
    setNavLocked(true);
    setFinishBackEnabled(false);

    if (finishStage) {
      finishStage.classList.remove("is-in");
      finishStage.classList.add("is-out");
    }

    await wait(450);
    if (token !== sequenceToken) {
      finishAnimating = false;
      return;
    }

    if (finishStage) {
      finishStage.classList.remove("is-out");
      finishStage.hidden = true;
    }
    if (storyEl) storyEl.classList.remove("is-finish-mode");
    document.body.classList.remove("is-finish-lock");

    if (floatEl) {
      floatEl.classList.remove("is-away", "is-leaving");
      clearPanelMotionStyles();
      void floatEl.offsetWidth;
      floatEl.classList.add("is-entering");
    }

    await wait(FINISH_ENTER_MS);
    if (token !== sequenceToken) {
      finishAnimating = false;
      return;
    }

    if (floatEl) {
      floatEl.classList.remove("is-entering");
      clearPanelMotionStyles();
      setFlatTransform(0);
    }

    finishAnimating = false;
    setNavLocked(false);
    setFinishBackEnabled(false);
  }

  async function applyTour(beat, token, opts) {
    var tour = beat.tour || {};
    clearTourEffects();

    if (tour.clearTable || (!tour.fillSample && tableBody && tableBody.dataset.mode !== "empty")) {
      if (!tour.fillSample) renderEmptyTable();
    }

    if (tour.fillSample && !opts.skipSequence && tour.animateFill && tableBody && tableBody.dataset.mode !== "sample") {
      await playFillSequence(token);
      if (token !== sequenceToken) return;
    } else if (tour.fillSample) {
      renderSampleInstant();
    } else if (tour.clearTable) {
      renderEmptyTable();
    }

    if (tour.spotlight) {
      var spot = document.getElementById(tour.spotlight);
      if (spot) spot.classList.add("is-spotlight");
    }

    if (tour.spotlightFlows) {
      document.querySelectorAll(".flow-btn").forEach(function (el) {
        el.classList.add("is-spotlight");
      });
    }

    if (tour.glowHeaders) {
      document.querySelectorAll("#data-table th").forEach(function (th) {
        th.classList.add("is-col-glow");
      });
    }

    if (tour.markInputDone && btnLoadJson) {
      btnLoadJson.classList.add("completed");
      if (tour.spotlight !== "btnLoadJson") {
        btnLoadJson.classList.remove("is-spotlight");
      }
      setLogLine("[demo] Jersey list loaded (3 rows)");
    }

    if (tour.animateOutput && !opts.skipSequence) {
      setNavLocked(true);
      if (btnSelectFolder) {
        btnSelectFolder.classList.add("is-spotlight");
        btnSelectFolder.classList.remove("completed");
      }
      await wait(520);
      if (token !== sequenceToken) return;
      if (btnSelectFolder) {
        btnSelectFolder.classList.add("completed");
        btnSelectFolder.classList.remove("is-spotlight");
      }
      setLogLine("[demo] Output folder selected");
      setNavLocked(false);
    } else if (tour.markOutputDone && btnSelectFolder) {
      btnSelectFolder.classList.add("completed");
      if (tour.spotlight !== "btnSelectFolder") {
        btnSelectFolder.classList.remove("is-spotlight");
      }
      setLogLine("[demo] Output folder selected");
    }

    if (tour.resetExport) {
      resetExportOptions();
      var eps = document.getElementById("enableEps");
      var epsGroup = document.getElementById("epsOptionsGroup");
      if (eps) eps.checked = true;
      if (epsGroup) epsGroup.classList.add("active");
    }

    if (tour.animateExport && !opts.skipSequence) {
      await animateExportSelect(token);
      if (token !== sequenceToken) return;
    } else if (tour.animateExport || tour.applyExport) {
      resetExportOptions();
      applyNeededExports();
      setLogLine("[demo] Export selected: TIF + EPS + JPG + PDF");
    }

    if (tour.animateStart && !opts.skipSequence) {
      await animateStartClick(token);
      if (token !== sequenceToken) return;
    } else if (tour.animateStart) {
      if (btnStart) {
        btnStart.classList.add("is-started");
        btnStart.textContent = "START";
      }
      setLogLine("[demo] Process started");
    }

    if (tour.keepStartDone && btnStart) {
      btnStart.classList.add("is-started");
      btnStart.textContent = "START";
    }

    if (tour.panelExit) {
      setLogLine("[demo] Export complete — check your OUTPUT folder");
    }

    if (tour.extra === "datasheet") renderDatasheetExtra();
    else if (tour.extra === "layouts") renderLayoutsExtra();
    else hideExtra();
  }

  function clearFields() {
    if (userInput) {
      userInput.value = "";
      userInput.classList.remove("is-typing");
    }
    if (passInput) {
      passInput.value = "";
      passInput.classList.remove("is-typing");
    }
    if (loginBtn) loginBtn.classList.remove("is-pulse", "is-done");
  }

  function fillInstant() {
    if (userInput) {
      userInput.value = USERNAME;
      userInput.classList.remove("is-typing");
    }
    if (passInput) {
      passInput.value = PASSWORD;
      passInput.classList.remove("is-typing");
    }
    if (loginBtn) {
      loginBtn.classList.remove("is-pulse");
      loginBtn.classList.add("is-done");
    }
  }

  async function playLoginSequence(token, keepLocked) {
    clearFields();
    setNavLocked(true);
    showFace("login");

    if (userInput) userInput.classList.add("is-typing");
    await wait(180);
    if (token !== sequenceToken) return;
    if (userInput) {
      userInput.value = USERNAME;
      userInput.classList.remove("is-typing");
    }

    await wait(200);
    if (token !== sequenceToken) return;
    if (passInput) passInput.classList.add("is-typing");
    await wait(180);
    if (token !== sequenceToken) return;
    if (passInput) {
      passInput.value = PASSWORD;
      passInput.classList.remove("is-typing");
    }

    await wait(200);
    if (token !== sequenceToken) return;
    if (loginBtn) {
      loginBtn.classList.add("is-pulse");
      await wait(360);
      if (token !== sequenceToken) return;
      loginBtn.classList.remove("is-pulse");
      loginBtn.classList.add("is-done");
    }

    await wait(280);
    if (token !== sequenceToken) return;

    if (!keepLocked) {
      setNavLocked(false);
      updateNav(currentBeat, BEATS[currentBeat]);
    }
  }

  function setFlatTransform(angle) {
    if (!floatEl) return;
    yAngle = angle;
    floatEl.style.transform = "rotateY(" + angle + "deg)";
  }

  function showFace(face) {
    var isMain = face === "main";
    if (loginPanel) loginPanel.classList.toggle("is-front", !isMain);
    if (mainPanel) mainPanel.classList.toggle("is-front", isMain);
    if (isMain && appBody) appBody.scrollTop = 0;
  }

  async function spinY(delta, targetFace, token) {
    if (!floatEl) return;
    spinning = true;
    setNavLocked(true);

    var end = yAngle + delta;
    floatEl.classList.add("is-spinning");
    floatEl.style.transition =
      "transform " + SPIN_MS + "ms cubic-bezier(0.4, 0, 0.2, 1)";
    void floatEl.offsetWidth;
    setFlatTransform(end);

    await wait(SPIN_MS * 0.48);
    if (token !== sequenceToken) {
      spinning = false;
      return;
    }
    showFace(targetFace);

    await wait(SPIN_MS * 0.52);
    if (token !== sequenceToken) {
      spinning = false;
      return;
    }

    floatEl.classList.remove("is-spinning");
    floatEl.style.transition = "none";
    setFlatTransform(0);
    void floatEl.offsetWidth;
    floatEl.style.transition = "";

    spinning = false;
    setNavLocked(false);
  }

  function applyCopy(beat) {
    titleEl.textContent = t(beat.title);
    hintEl.textContent = t(beat.hint);
    copy?.classList.toggle("is-complete", beat.id === "finish");
    updateStepTabs(currentBeat);
  }

  function renderStepTabs() {
    if (!storyStepsEl) return;
    storyStepsEl.innerHTML = STEP_TABS.map(function (tab) {
      return (
        '<button type="button" class="story__step-btn" role="tab" data-beat="' +
        tab.beatIndex +
        '" data-step="' +
        tab.key +
        '">' +
        tab.label +
        "</button>"
      );
    }).join("");

    storyStepsEl.querySelectorAll(".story__step-btn").forEach(function (btn) {
      btn.addEventListener("click", function () {
        var i = parseInt(btn.getAttribute("data-beat"), 10);
        if (isNaN(i)) return;
        jumpToBeat(i);
      });
    });
  }

  function updateStepTabs(index) {
    if (!storyStepsEl) return;
    var beat = BEATS[index];
    var key = beat && beat.stepNum ? beat.stepNum.en : "";
    var busy = spinning || finishAnimating;
    storyStepsEl.querySelectorAll(".story__step-btn").forEach(function (btn) {
      var active = btn.getAttribute("data-step") === key;
      btn.classList.toggle("is-active", active);
      btn.setAttribute("aria-selected", active ? "true" : "false");
      btn.disabled = busy;
    });
  }

  function jumpToBeat(index) {
    if (spinning || finishAnimating) return;
    var next = clamp(index, 0, BEATS.length - 1);
    if (next === currentBeat) return;
    pendingGo = null;
    stopVoice();
    applyBeat(next);
  }

  function setNavLocked(locked) {
    var busy = locked || spinning || finishAnimating;
    if (btnPrev) btnPrev.disabled = busy || currentBeat <= 0;
    if (btnNext) {
      var atEnd = currentBeat >= BEATS.length - 1;
      btnNext.disabled = busy || atEnd;
    }
    updateStepTabs(currentBeat);
  }

  function updateNav(index, beat) {
    var busy = spinning || finishAnimating;
    if (btnPrev) {
      btnPrev.disabled = index <= 0 || busy;
      btnPrev.textContent = t(UI.back);
    }
    if (btnNext) {
      var atEnd = index >= BEATS.length - 1;
      btnNext.textContent = t(beat.nextLabel) || (atEnd ? t(UI.done) : t(UI.next));
      btnNext.disabled = atEnd || busy;
    }
    updateStepTabs(index);
  }

  async function applyBeat(index, options) {
    var opts = options || {};
    var prev = currentBeat;
    var beat = BEATS[index];
    if (!beat) return;

    applyInFlight += 1;
    currentBeat = index;
    sequenceToken += 1;
    var token = sequenceToken;
    pendingGo = null;
    stopVoice();

    try {
    applyCopy(beat);

    if (progressFill) {
      progressFill.style.width =
        Math.round((index / (BEATS.length - 1)) * 100) + "%";
    }

    var prevBeat = BEATS[prev];
    var prevFace = prevBeat ? prevBeat.face : "login";
    var goingToMain = beat.face === "main" && prevFace !== "main";
    var leavingMain = beat.face !== "main" && prevFace === "main";
    var enteringFinish = beat.id === "finish";
    var leavingFinish = prevBeat && prevBeat.id === "finish" && beat.id !== "finish";
    var shouldSpeak = opts.speak !== false && !opts.skipSequence;

    if (leavingFinish) {
      if (!opts.skipSequence) await exitFinishMode(token);
      else hideFinishModeInstant();
      if (token !== sequenceToken) return;

      if (floatEl) {
        floatEl.style.transition = "none";
        setFlatTransform(0);
        showFace(beat.face);
        void floatEl.offsetWidth;
        floatEl.style.transition = "";
      } else {
        showFace(beat.face);
      }

      fillInstant();
      await applyTour(beat, token, Object.assign({}, opts, { skipSequence: true }));
      if (token !== sequenceToken) return;
      if (shouldSpeak) speakBeat(beat, "main", token);
      updateNav(index, beat);
      return;
    }

    if (enteringFinish) {
      if (floatEl && !floatEl.classList.contains("is-away")) {
        floatEl.style.transition = "none";
        setFlatTransform(0);
        showFace(beat.face);
        void floatEl.offsetWidth;
        floatEl.style.transition = "";
      } else {
        showFace(beat.face);
      }

      fillInstant();
      await applyTour(beat, token, Object.assign({}, opts, { skipSequence: true }));
      if (token !== sequenceToken) return;

      if (!opts.skipSequence) await enterFinishMode(token);
      else showFinishModeInstant();
      if (token !== sequenceToken) return;

      if (opts.speak || shouldSpeak) speakBeat(beat, "main", token);
      updateNav(index, beat);
      return;
    }

    if (storyEl && storyEl.classList.contains("is-finish-mode")) {
      hideFinishModeInstant();
    }

    if (!opts.skipSequence && goingToMain) {
      updateNav(index, beat);
      if (beat.runLoginThenMain) {
        titleEl.textContent = t(UI.loginMidTitle);
        hintEl.textContent = t(UI.loginMidHint);
        if (shouldSpeak) speakBeat(beat, "login", token);
        await playLoginSequence(token, true);
        if (token !== sequenceToken) return;
        applyCopy(beat);
        if (shouldSpeak) speakBeat(beat, "main", token);
      } else {
        fillInstant();
        if (shouldSpeak) speakBeat(beat, "main", token);
      }
      await spinY(360, "main", token);
      if (token !== sequenceToken) return;
      await applyTour(beat, token, opts);
      updateNav(index, beat);
      return;
    }

    if (!opts.skipSequence && leavingMain) {
      clearTourEffects();
      hideExtra();
      updateNav(index, beat);
      await spinY(-360, beat.face, token);
      if (token !== sequenceToken) return;
      if (!beat.loggedIn) clearFields();
      else fillInstant();
      if (shouldSpeak) speakBeat(beat, "main", token);
      updateNav(index, beat);
      return;
    }

    if (floatEl) {
      floatEl.style.transition = "none";
      setFlatTransform(0);
      showFace(beat.face);
      void floatEl.offsetWidth;
      floatEl.style.transition = "";
    } else {
      showFace(beat.face);
    }

    if (!beat.loggedIn) {
      clearTourEffects();
      hideExtra();
      clearFields();
      renderEmptyTable();
      updateNav(index, beat);
      if (opts.speak || shouldSpeak) speakBeat(beat, "main", token);
      return;
    }

    if (beat.runLoginSequence && !opts.skipSequence) {
      clearTourEffects();
      hideExtra();
      if (shouldSpeak) speakBeat(beat, "login", token);
      playLoginSequence(token, false);
      return;
    }

    fillInstant();
    await applyTour(beat, token, opts);
    if (token !== sequenceToken) return;
    if (opts.speak || shouldSpeak) speakBeat(beat, "main", token);
    updateNav(index, beat);
    } finally {
      applyInFlight = Math.max(0, applyInFlight - 1);
      // Voice may have ended while this beat was still animating
      if (applyInFlight === 0 && !voicePlaying) flushPendingGo();
    }
  }

  function go(delta) {
    if (spinning || finishAnimating) return;
    var next = clamp(currentBeat + delta, 0, BEATS.length - 1);
    if (next === currentBeat) return;
    // Wait for voice to finish — keep buttons enabled, queue the click
    if (voicePlaying) {
      pendingGo = delta;
      return;
    }
    applyBeat(next);
  }

  btnNext?.addEventListener("click", function () {
    go(1);
  });
  btnPrev?.addEventListener("click", function () {
    go(-1);
  });
  btnFinishBack?.addEventListener("click", function () {
    go(-1);
  });

  document.querySelectorAll(".lang-switch__btn").forEach(function (btn) {
    btn.addEventListener("click", function () {
      setLanguage(btn.getAttribute("data-lang"), { speak: true });
    });
  });

  document.querySelectorAll(".lang-modal__pick").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var pick = btn.getAttribute("data-lang");
      closeLangModal();
      setLanguage(pick, { skipRefresh: true, speak: false });
      applyBeat(currentBeat, { skipSequence: true, speak: true });
    });
  });

  window.addEventListener("resize", function () {
    syncPanelScale();
    applyBeat(currentBeat, { skipSequence: true, speak: false });
  });

  syncPanelScale();
  renderEmptyTable();
  renderStepTabs();
  var needsLangPick = initLanguage();
  syncLangUI();
  applyBeat(0, { skipSequence: true, speak: !needsLangPick });

  window.PatternFlowUsermap = {
    beats: BEATS,
    getLang: function () {
      return lang;
    },
    setLang: setLanguage,
    goToBeat: function (i) {
      applyBeat(clamp(i, 0, BEATS.length - 1));
    },
  };
})();
