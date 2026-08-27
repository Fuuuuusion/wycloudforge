(function () {
  "use strict";

  // ---------- 模拟数据 ----------
  var SONGS = [
    { t: "晴天", a: "周杰伦", al: "叶惠美", d: 269, g: 0 },
    { t: "稻香", a: "周杰伦", al: "魔杰座", d: 223, g: 1 },
    { t: "七里香", a: "周杰伦", al: "七里香", d: 299, g: 2 },
    { t: "平凡之路", a: "朴树", al: "猎户星座", d: 302, g: 3 },
    { t: "海阔天空", a: "Beyond", al: "乐与怒", d: 326, g: 4 },
    { t: "光年之外", a: "邓紫棋", al: "光年之外", d: 235, g: 5 },
    { t: "成都", a: "赵雷", al: "无法长大", d: 328, g: 6 },
    { t: "消愁", a: "毛不易", al: "平凡的一天", d: 256, g: 7 },
    { t: "夜空中最亮的星", a: "逃跑计划", al: "世界", d: 271, g: 0 },
    { t: "往后余生", a: "马良", al: "往后余生", d: 246, g: 1 },
    { t: "理想", a: "赵雷", al: "吉姆餐厅", d: 305, g: 2 },
    { t: "红玫瑰", a: "陈奕迅", al: "认了吧", d: 279, g: 3 }
  ];

  var PLAYLISTS = [
    { name: "我喜欢的音乐", g: 4, meta: "12 首 · 45 分钟" },
    { name: "华语经典", g: 0, meta: "32 首 · 1 小时 58 分" },
    { name: "深夜电台", g: 6, meta: "18 首 · 1 小时 12 分" },
    { name: "跑步歌单", g: 5, meta: "24 首 · 1 小时 30 分" }
  ];

  var ARTISTS = [
    { name: "周杰伦", n: 32, g: 0 }, { name: "朴树", n: 8, g: 3 },
    { name: "Beyond", n: 21, g: 4 }, { name: "邓紫棋", n: 17, g: 5 },
    { name: "赵雷", n: 12, g: 6 }, { name: "毛不易", n: 9, g: 7 },
    { name: "陈奕迅", n: 28, g: 1 }, { name: "逃跑计划", n: 6, g: 2 }
  ];

  var ALBUMS = [
    { name: "叶惠美", artist: "周杰伦", g: 0 }, { name: "魔杰座", artist: "周杰伦", g: 1 },
    { name: "七里香", artist: "周杰伦", g: 2 }, { name: "猎户星座", artist: "朴树", g: 3 },
    { name: "乐与怒", artist: "Beyond", g: 4 }, { name: "无法长大", artist: "赵雷", g: 6 }
  ];

  var LYRICS = [
    "风吹过 雨落下", "我们在人海里相遇", "每一首歌 都是一段回忆",
    "按下播放键 让时间慢下来", "白天不懂夜的黑", "我们终将到达远方",
    "旋律穿过耳膜 抵达心底", "把喜欢的歌 装进口袋", "晚安 好梦",
    "让音乐陪你度过今夜"
  ];

  var MODES = [
    { icon: "loop", tip: "列表循环" },
    { icon: "single", tip: "单曲循环" },
    { icon: "shuffle", tip: "随机播放" }
  ];

  // ---------- 状态 ----------
  var state = {
    playing: false,
    pos: 0,
    cur: 0,
    mode: 0,
    volume: 70,
    muted: false,
    favorite: false,
    currentList: SONGS
  };

  // ---------- 本地导入(演示用) ----------
  var AUDIO_EXTS = ["mp3", "flac", "wav", "m4a", "aac", "ogg", "opus"];
  var currentPlaylistName = "我喜欢的音乐";

  function hashStr(s) {
    var h = 0;
    for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
    return h;
  }
  function loadAddedSongs() {
    try {
      var arr = JSON.parse(localStorage.getItem("neteaseProtoAdded") || "[]");
      arr.forEach(function (x) {
        if (x && x.t) {
          x.added = true;
          SONGS.push(x);
        }
      });
    } catch (e) { /* 忽略损坏的本地数据 */ }
  }
  function saveAddedSongs() {
    try {
      localStorage.setItem("neteaseProtoAdded", JSON.stringify(SONGS.filter(function (s) { return s.added; })));
    } catch (e) { /* 隐私模式下可能失败,忽略 */ }
  }
  function showToast(msg) {
    var t = $("toast");
    t.textContent = msg;
    t.classList.add("show");
    clearTimeout(showToast._tm);
    showToast._tm = setTimeout(function () { t.classList.remove("show"); }, 2200);
  }
  function openSettings() { $("settingsPanel").classList.remove("hidden"); }
  function closeSettings() { $("settingsPanel").classList.add("hidden"); }
  function wireSettings() {
    document.querySelector('.wc-btn[title="设置"]').addEventListener("click", function () {
      if ($("settingsPanel").classList.contains("hidden")) openSettings();
      else closeSettings();
    });
    $("closeSettingsBtn").addEventListener("click", closeSettings);
    $("rescanBtn").addEventListener("click", function () {
      showToast("已重新扫描(演示)");
      refreshViews();
    });
    $("lyricSizeRange").addEventListener("input", function () {
      var v = this.value;
      document.documentElement.style.setProperty("--lyric-size", v + "px");
      $("lyricSizeValue").textContent = v;
    });
  }
  function attachDuration(file, song) {
    try {
      var url = URL.createObjectURL(file);
      var audio = document.createElement("audio");
      audio.preload = "metadata";
      audio.onloadedmetadata = function () {
        song.d = Math.round(audio.duration) || 0;
        URL.revokeObjectURL(url);
        refreshViews();
      };
      audio.onerror = function () { URL.revokeObjectURL(url); };
      audio.src = url;
    } catch (e) { /* 忽略 */ }
  }
  function addFiles(files) {
    var added = 0;
    Array.prototype.forEach.call(files, function (f) {
      var ext = (f.name.split(".").pop() || "").toLowerCase();
      var isAudio = AUDIO_EXTS.indexOf(ext) >= 0 || (f.type && f.type.indexOf("audio/") === 0);
      if (!isAudio) return;
      var stem = f.name.replace(/\.[^.]+$/, "");
      var song = { t: stem, a: "", al: "", d: 0, g: hashStr(f.name) % 8, added: true };
      SONGS.push(song);
      added++;
      attachDuration(f, song);
    });
    if (!added) {
      showToast("未找到音频文件(支持 mp3/flac/wav/m4a/aac/ogg)");
      return;
    }
    saveAddedSongs();
    refreshViews();
    showToast("已添加 " + added + " 首(仅演示列表,未保存音频本体)");
  }
  function refreshViews() {
    renderDiscover();
    renderLibrary();
    var q = $("searchInput").value.trim();
    if (q) {
      var evt = new Event("input");
      $("searchInput").dispatchEvent(evt);
    }
    if ($("page-playlist").classList.contains("active"))
      openPlaylist(currentPlaylistName);
  }

  // ---------- 工具 ----------
  function fmt(s) {
    s = Math.max(0, Math.round(s));
    var m = Math.floor(s / 60), sec = s % 60;
    return (m < 10 ? "0" + m : m) + ":" + (sec < 10 ? "0" + sec : sec);
  }
  function coverHtml(g, cls) {
    return '<div class="' + (cls || "song-cover") + " g" + g + '"></div>';
  }
  function escapeHtml(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }
  function highlight(text, q) {
    if (!q) return escapeHtml(text);
    var idx = text.toLowerCase().indexOf(q.toLowerCase());
    if (idx < 0) return escapeHtml(text);
    return escapeHtml(text.slice(0, idx)) + "<mark>" + escapeHtml(text.slice(idx, idx + q.length)) + "</mark>" + escapeHtml(text.slice(idx + q.length));
  }

  // ---------- 渲染:歌曲行 ----------
  function renderSongList(container, list, opts) {
    opts = opts || {};
    container.innerHTML = "";
    list.forEach(function (song, i) {
      var row = document.createElement("div");
      row.className = "song-row" + (i === state.cur ? " playing" : "");
      row.innerHTML =
        '<span class="c-idx">' + (i + 1) + '</span>' +
        '<span class="row-play"><svg class="ic"><use href="#i-play"/></svg></span>' +
        coverHtml(song.g) +
        '<span class="c-title">' + (opts.q ? highlight(song.t, opts.q) : escapeHtml(song.t)) + '</span>' +
        '<span class="c-artist">' + (opts.q ? highlight(song.a, opts.q) : escapeHtml(song.a)) + '</span>' +
        '<span class="c-album">' + (opts.q ? highlight(song.al, opts.q) : escapeHtml(song.al)) + '</span>' +
        '<span class="c-dur">' + fmt(song.d) + '</span>';
      row.addEventListener("click", function () { playFrom(list, i); });
      container.appendChild(row);
    });
  }

  function renderCoverCard(cover, name, sub, click) {
    var card = document.createElement("div");
    card.className = "card";
    card.innerHTML =
      '<div class="card-cover g' + cover + '"><span class="play-hint"><svg class="ic"><use href="#i-play"/></svg></span></div>' +
      '<div class="card-name">' + escapeHtml(name) + '</div>' +
      (sub ? '<div class="card-sub">' + escapeHtml(sub) + '</div>' : "");
    card.addEventListener("click", click);
    return card;
  }

  // ---------- 页面切换 ----------
  var pages = document.querySelectorAll(".page");
  var navItems = document.querySelectorAll(".nav-item");
  function showPage(id) {
    pages.forEach(function (p) { p.classList.toggle("active", p.id === "page-" + id); });
    navItems.forEach(function (n) {
      n.classList.toggle("active", n.dataset.page === id);
    });
  }
  navItems.forEach(function (n) {
    n.addEventListener("click", function () { showPage(n.dataset.page); });
  });

  // ---------- 播放 ----------
  var $ = function (id) { return document.getElementById(id); };
  function playFrom(list, idx) {
    state.currentList = list;
    state.cur = idx;
    state.pos = 0;
    state.playing = true;
    updateAll();
  }
  function nextSong() {
    var n = state.currentList.length;
    if (!n) return;
    if (state.mode === 2) {
      state.cur = Math.floor(Math.random() * n);
    } else {
      state.cur = (state.cur + 1) % n;
    }
    state.pos = 0;
    updateAll();
  }
  function prevSong() {
    var n = state.currentList.length;
    if (!n) return;
    if (state.pos > 3000) { state.pos = 0; updateAll(); return; }
    state.cur = (state.cur - 1 + n) % n;
    state.pos = 0;
    updateAll();
  }

  // ---------- 播放栏更新 ----------
  var lastLyricIdx = -1;
  function updateAll() {
    var song = state.currentList[state.cur];
    if (!song) return;
    // 播放栏
    $("nowCover").className = "now-cover g" + song.g;
    $("nowTitle").textContent = song.t;
    $("nowArtist").textContent = song.a + " - " + song.al;
    $("timeTotal").textContent = fmt(song.d);
    // 正在播放页
    $("bigCover").className = "big-cover g" + song.g;
    $("playingBg").className = "playing-bg g" + song.g;
    $("lyricsTitle").textContent = song.t + " - " + song.a;
    // 播放/暂停图标
    var playSvg = $("playBtn").querySelectorAll(".ic");
    playSvg[0].classList.toggle("hidden", state.playing);
    playSvg[1].classList.toggle("hidden", !state.playing);
    $("playBtn").title = state.playing ? "暂停" : "播放";
    // 列表高亮
    document.querySelectorAll(".song-row.playing").forEach(function (r) { r.classList.remove("playing"); });
    var lists = [$("libSongList"), $("plSongList"), $("searchSongList")];
    lists.forEach(function (l) {
      if (!l) return;
      var rows = l.children;
      for (var i = 0; i < rows.length; i++) {
        if (state.currentList[i] === song) { rows[i].classList.add("playing"); break; }
      }
    });
    renderLyrics();
  }

  function renderLyrics() {
    var box = $("lyrics");
    box.innerHTML = "";
    LYRICS.forEach(function (line, i) {
      var el = document.createElement("div");
      el.className = "lyric-line" + (i === 0 ? " active" : "");
      el.textContent = line;
      box.appendChild(el);
    });
    lastLyricIdx = -1;
  }

  function updateProgress() {
    var song = state.currentList[state.cur];
    if (!song) return;
    var dur = song.d * 1000;
    if (state.pos > dur) { state.pos = dur; }
    var pct = dur ? (state.pos / dur) * 100 : 0;
    $("timeCur").textContent = fmt(state.pos / 1000);
    $("timeFill").style.width = pct + "%";
    // 歌词高亮
    var idx = Math.min(LYRICS.length - 1, Math.floor(state.pos / 4000));
    if (idx !== lastLyricIdx) {
      lastLyricIdx = idx;
      var lines = $("lyrics").children;
      for (var i = 0; i < lines.length; i++) {
        lines[i].classList.toggle("active", i === idx);
      }
    }
  }

  setInterval(function () {
    if (!state.playing) return;
    state.pos += 250;
    var song = state.currentList[state.cur];
    if (song && state.pos >= song.d * 1000) {
      if (state.mode === 1) { state.pos = 0; }
      else { nextSong(); }
    }
    updateProgress();
  }, 250);

  // ---------- 播放控制按钮 ----------
  $("playBtn").addEventListener("click", function () {
    state.playing = !state.playing;
    updateAll();
  });
  $("prevBtn").addEventListener("click", prevSong);
  $("nextBtn").addEventListener("click", nextSong);
  $("modeBtn").addEventListener("click", function () {
    state.mode = (state.mode + 1) % 3;
    var svgs = $("modeBtn").querySelectorAll(".ic");
    svgs.forEach(function (s, i) { s.classList.toggle("hidden", i !== state.mode); });
    $("modeBtn").title = MODES[state.mode].tip;
  });
  $("timeSlider").addEventListener("click", function (e) {
    var r = this.getBoundingClientRect();
    state.pos = ((e.clientX - r.left) / r.width) * state.currentList[state.cur].d * 1000;
    updateProgress();
  });
  $("volSlider").addEventListener("click", function (e) {
    var r = this.getBoundingClientRect();
    state.volume = Math.max(0, Math.min(100, ((e.clientX - r.left) / r.width) * 100));
    state.muted = false;
    updateVolume();
  });
  $("muteBtn").addEventListener("click", function () {
    state.muted = !state.muted;
    updateVolume();
  });
  $("heartBtn").addEventListener("click", function () {
    state.favorite = !state.favorite;
    var svgs = this.querySelectorAll(".ic");
    svgs[0].classList.toggle("hidden", state.favorite);
    svgs[1].classList.toggle("hidden", !state.favorite);
    this.classList.toggle("on", state.favorite);
  });
  function updateVolume() {
    var v = state.muted ? 0 : state.volume;
    $("volFill").style.width = v + "%";
    var svgs = $("muteBtn").querySelectorAll(".ic");
    var muted = state.muted || state.volume === 0;
    svgs[0].classList.toggle("hidden", muted);
    svgs[1].classList.toggle("hidden", !muted);
  }
  updateVolume();

  // ---------- 首页渲染 ----------
  function renderDiscover() {
    var recent = $("recentRow");
    recent.innerHTML = "";
    SONGS.slice(0, 8).forEach(function (s, i) {
      var card = document.createElement("div");
      card.className = "mini-card";
      card.innerHTML =
        '<div class="mini-cover g' + s.g + '"></div>' +
        '<div class="mini-name">' + escapeHtml(s.t) + '</div>' +
        '<div class="mini-sub">' + escapeHtml(s.a) + '</div>';
      card.addEventListener("click", function () { playFrom(SONGS, i); });
      recent.appendChild(card);
    });

    var plg = $("playlistGrid");
    plg.innerHTML = "";
    PLAYLISTS.forEach(function (p) {
      plg.appendChild(renderCoverCard(p.g, p.name, p.meta, function () { openPlaylist(p.name); }));
    });
    var newCard = document.createElement("div");
    newCard.className = "card new-card";
    newCard.innerHTML = '<div class="new-icon"><svg class="ic"><use href="#i-plus"/></svg></div><div class="card-name">创建歌单</div>';
    newCard.addEventListener("click", createPlaylist);
    plg.appendChild(newCard);

    var rec = $("recommendGrid");
    rec.innerHTML = "";
    var recs = [SONGS[3], SONGS[4], SONGS[6], SONGS[0], SONGS[8], SONGS[9], SONGS[5], SONGS[2]];
    recs.forEach(function (s, i) {
      rec.appendChild(renderCoverCard(s.g, s.al + " · 精选", s.a + " 等 " + (i + 8) + " 首", function () { playFrom(recs, i); }));
    });

    var ar = $("artistRow");
    ar.innerHTML = "";
    ARTISTS.slice(0, 6).forEach(function (a) {
      var item = document.createElement("div");
      item.className = "artist-item";
      item.innerHTML =
        '<div class="artist-avatar g' + a.g + '"><svg class="ic ic-cover"><use href="#i-music"/></svg></div>' +
        '<div class="artist-name">' + escapeHtml(a.name) + '</div>' +
        '<div class="artist-sub">' + a.n + " 首" + '</div>';
      item.addEventListener("click", function () { showPage("library"); });
      ar.appendChild(item);
    });
  }

  // ---------- 音乐库 ----------
  function bindTabs(tabsId, panelPrefix) {
    var tabs = $(tabsId).children;
    Array.prototype.forEach.call(tabs, function (tab) {
      tab.addEventListener("click", function () {
        Array.prototype.forEach.call(tabs, function (t) { t.classList.toggle("active", t === tab); });
        var panels = document.querySelectorAll("." + panelPrefix);
        panels.forEach(function (p) { p.classList.toggle("active", p.id === panelPrefix + "-" + tab.dataset.tab); });
      });
    });
  }
  bindTabs("libTabs", "tab");
  bindTabs("searchTabs", "tab");

  function renderLibrary() {
    renderSongList($("libSongList"), SONGS);
    var ag = $("artistGrid");
    ag.innerHTML = "";
    ARTISTS.forEach(function (a) {
      ag.appendChild(renderCoverCard(a.g, a.name, a.n + " 首", function () {}));
    });
    var alg = $("albumGrid");
    alg.innerHTML = "";
    ALBUMS.forEach(function (a) {
      alg.appendChild(renderCoverCard(a.g, a.name, a.artist, function () {}));
    });
  }

  // ---------- 歌单 ----------
  function openPlaylist(name) {
    currentPlaylistName = name;
    var pl = PLAYLISTS.filter(function (p) { return p.name === name; })[0] || PLAYLISTS[0];
    $("plName").textContent = pl.name;
    $("plMeta").textContent = pl.meta;
    document.querySelector(".pl-cover").className = "pl-cover g" + pl.g;
    renderSongList($("plSongList"), SONGS);
    showPage("playlist");
    document.querySelectorAll(".playlist-item").forEach(function (item) {
      item.classList.toggle("active", item.dataset.playlist === name);
    });
  }
  document.querySelectorAll(".playlist-item").forEach(function (item) {
    item.addEventListener("click", function () { openPlaylist(item.dataset.playlist); });
  });
  $("playAllBtn").addEventListener("click", function () { playFrom(SONGS, 0); });
  $("lyricsPageBtn").addEventListener("click", function () { showPage("playing"); });
  $("bannerPlayBtn").addEventListener("click", function () {
    if (SONGS.length)
      playFrom(SONGS, Math.floor(Math.random() * SONGS.length));
  });
  $("bannerImportBtn").addEventListener("click", function () { $("fileInput").click(); });
  $("addSongsBtn").addEventListener("click", function () { $("fileInput").click(); });
  $("fileInput").addEventListener("change", function () {
    addFiles(this.files);
    this.value = "";
  });
  document.addEventListener("dragover", function (e) { e.preventDefault(); });
  document.addEventListener("drop", function (e) {
    e.preventDefault();
    if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length)
      addFiles(e.dataTransfer.files);
  });

  function createPlaylist() {
    var name = prompt("输入歌单名称:");
    if (!name || !name.trim()) return;
    name = name.trim();
    PLAYLISTS.push({ name: name, g: Math.floor(Math.random() * 8), meta: "0 首" });
    var item = document.createElement("div");
    item.className = "playlist-item";
    item.dataset.playlist = name;
    item.innerHTML = '<svg class="ic"><use href="#i-music"/></svg>' + escapeHtml(name);
    item.addEventListener("click", function () { openPlaylist(name); });
    document.querySelector(".nav-section").appendChild(item);
    renderDiscover();
    openPlaylist(name);
  }
  $("createPlaylist").addEventListener("click", createPlaylist);

  // ---------- 搜索 ----------
  $("searchInput").addEventListener("input", function () {
    var q = this.value.trim();
    if (!q) { showPage("discover"); return; }
    showPage("search");
    $("searchTitle").textContent = '搜索 "' + q + '"';
    var hit = SONGS.filter(function (s) {
      return s.t.toLowerCase().indexOf(q.toLowerCase()) >= 0 ||
        s.a.toLowerCase().indexOf(q.toLowerCase()) >= 0 ||
        s.al.toLowerCase().indexOf(q.toLowerCase()) >= 0;
    });
    $("searchEmpty").style.display = hit.length ? "none" : "block";
    renderSongList($("searchSongList"), hit, { q: q });
  });

  // ---------- 初始化 ----------
  loadAddedSongs();
  renderDiscover();
  renderLibrary();
  openPlaylist("我喜欢的音乐");
  wireSettings();
  updateAll();
  updateProgress();
})();
