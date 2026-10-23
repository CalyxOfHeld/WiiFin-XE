/* WiiFin-XE site: search across every page, the contents' highlight, and
 * the Wii Menu clock on the home page.  The pages read fine without it. */
(function () {
  "use strict";

  /* ---- search ---- */
  var data = [];
  try { data = JSON.parse(document.getElementById("search-data").textContent); } catch (e) { data = []; }
  var q = document.getElementById("q");
  var list = document.getElementById("results");
  var sel = -1, shown = [];

  function words(s) { return s.toLowerCase().split(/\s+/).filter(Boolean); }
  function esc(s) {
    return s.replace(/[&<>"]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]; });
  }
  function mark(text, ws) {
    var out = esc(text);
    ws.forEach(function (w) {
      var re = new RegExp("(" + w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")", "gi");
      out = out.replace(re, "<mark>$1</mark>");
    });
    return out;
  }
  /* the part of the text around the first word found */
  function snippet(text, ws) {
    var lower = text.toLowerCase(), at = -1;
    for (var i = 0; i < ws.length && at < 0; i++) at = lower.indexOf(ws[i]);
    if (at < 0) return text.slice(0, 110) + (text.length > 110 ? "…" : "");
    var from = Math.max(0, at - 40);
    return (from > 0 ? "…" : "") + text.slice(from, from + 120) + (from + 120 < text.length ? "…" : "");
  }
  function score(e, ws) {
    var h = e.h.toLowerCase(), t = e.t.toLowerCase(), x = e.x.toLowerCase(), s = 0;
    for (var i = 0; i < ws.length; i++) {
      var w = ws[i], here = 0;
      if (h.indexOf(w) >= 0) here += 10;
      if (t.indexOf(w) >= 0) here += 3;
      if (x.indexOf(w) >= 0) here += 1;
      if (!here) return 0;           /* every word must be somewhere */
      s += here;
    }
    return s;
  }
  function render() {
    var ws = words(q.value);
    list.innerHTML = "";
    shown = [];
    sel = -1;
    if (!ws.length) { close(); return; }
    shown = data.map(function (e) { return { e: e, s: score(e, ws) }; })
      .filter(function (r) { return r.s > 0; })
      .sort(function (a, b) { return b.s - a.s; })
      .slice(0, 8)
      .map(function (r) { return r.e; });
    if (!shown.length) {
      list.innerHTML = '<li class="none">Nothing found for “' + esc(q.value.trim()) + '”</li>';
    }
    shown.forEach(function (e, i) {
      var li = document.createElement("li");
      li.setAttribute("role", "option");
      li.id = "r" + i;
      li.innerHTML = '<a href="' + e.u + '"><span class="where">' + esc(e.t) +
        (e.h !== e.t ? " › " + mark(e.h, ws) : "") + '</span><span class="what">' + mark(snippet(e.x, ws), ws) + "</span></a>";
      list.appendChild(li);
    });
    list.hidden = false;
    q.setAttribute("aria-expanded", "true");
  }
  function close() {
    list.hidden = true;
    q.setAttribute("aria-expanded", "false");
    q.removeAttribute("aria-activedescendant");
  }
  function move(d) {
    if (!shown.length) return;
    sel = (sel + d + shown.length) % shown.length;
    Array.prototype.forEach.call(list.querySelectorAll("li"), function (li, i) {
      li.classList.toggle("on", i === sel);
    });
    q.setAttribute("aria-activedescendant", "r" + sel);
  }
  if (q && list) {
    q.addEventListener("input", render);
    q.addEventListener("focus", function () { if (q.value) render(); });
    q.addEventListener("keydown", function (ev) {
      if (ev.key === "ArrowDown") { ev.preventDefault(); move(1); }
      else if (ev.key === "ArrowUp") { ev.preventDefault(); move(-1); }
      else if (ev.key === "Enter") {
        var target = shown[sel >= 0 ? sel : 0];
        if (target) { ev.preventDefault(); location.href = target.u; close(); }
      } else if (ev.key === "Escape") { q.value = ""; close(); q.blur(); }
    });
    document.addEventListener("click", function (ev) {
      if (!ev.target.closest || !ev.target.closest(".search")) close();
    });
    document.addEventListener("keydown", function (ev) {
      if (ev.key === "/" && document.activeElement !== q &&
          !/input|textarea|select/i.test(document.activeElement.tagName)) {
        ev.preventDefault();
        q.focus();
      }
    });
  }

  /* ---- the page list: folded on a phone, where it sits above the page ---- */
  var pages = document.querySelector(".sidebar details");
  if (pages && window.matchMedia && window.matchMedia("(max-width: 820px)").matches) pages.open = false;

  /* ---- "On this page": the section being read ---- */
  var toc = document.querySelectorAll(".toc a");
  if (toc.length && "IntersectionObserver" in window) {
    var byId = {};
    Array.prototype.forEach.call(toc, function (a) { byId[decodeURIComponent(a.hash.slice(1))] = a; });
    var heads = Array.prototype.filter.call(document.querySelectorAll(".doc h2[id]"), function (h) { return byId[h.id]; });
    var seen = new Set();
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) { if (en.isIntersecting) seen.add(en.target); else seen.delete(en.target); });
      var first = heads.filter(function (h) { return seen.has(h); })[0];
      if (!first) return;
      Array.prototype.forEach.call(toc, function (a) { a.classList.remove("on"); });
      byId[first.id].classList.add("on");
    }, { rootMargin: "-70px 0px -55% 0px" });
    heads.forEach(function (h) { io.observe(h); });
  }

  /* ---- the Wii Menu's clock and date, in the viewer's time ---- */
  var clock = document.getElementById("clock"), date = document.getElementById("date");
  if (clock && date) {
    var days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
    var tick = function () {
      var d = new Date();
      clock.textContent = String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");
      date.textContent = days[d.getDay()] + " " + (d.getMonth() + 1) + "/" + d.getDate();
    };
    tick();
    setInterval(tick, 10000);
  }
})();
