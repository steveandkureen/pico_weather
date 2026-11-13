/* Shared script for every layout. ES5 only: the wall display is an iPad 4
 * (iOS 10 Safari) - no fetch, no arrow functions, no let/const, no `??`.
 *
 * A layout is plain HTML + CSS with "slots":
 *   data-f="out_t"        text filled with a formatted value ("—" if missing)
 *   data-icon="now"       weather icon for current conditions
 *   data-rot="dir"        SVG <g> rotated to the wind direction (data-c="cx cy")
 *   data-list="fc"        container; its first child is a template row,
 *                         cloned once per forecast day (data-n = max rows).
 *     inside rows: data-k="name|date|cond|hi|lo|pp|pr" and data-icon="day",
 *                  data-bar="h" / "v" for the low→high range bar
 *   data-range="today"    today's range bar (layout C)
 * The firmware sends metric values; all unit conversion happens here.
 */
(function () {
  'use strict';

  var POLL_MS = 10000;
  var FAILS_BEFORE_WARNING = 3;

  var state = { data: null, lastOk: 0, fails: 0, override: null };

  var m = /[?&]layout=([a-dA-D])/.exec(location.search);
  if (m) state.override = m[1].toUpperCase();

  // ------------------------------------------------------------ units

  function cToUnit(cx100, u) {
    var c = cx100 / 100;
    return u === 'C' ? c : c * 9 / 5 + 32;
  }
  function cx10ToUnit(cx10, u) { return cToUnit(cx10 * 10, u); }
  function fmtTemp(cx100, u) {
    if (cx100 === null || cx100 === undefined) return '—';
    return Math.round(cToUnit(cx100, u)) + '°';
  }
  function fmtPress(pa, u) {
    if (pa === null || pa === undefined) return '—';
    if (u === 'hPa') return (pa / 100).toFixed(0);
    if (u === 'mmHg') return (pa / 133.322).toFixed(0);
    return (pa / 3386.39).toFixed(2);
  }
  function windFromMps(mps, u) {
    if (u === 'km/h') return mps * 3.6;
    if (u === 'm/s') return mps;
    if (u === 'kn') return mps * 1.94384;
    return mps * 2.23694;
  }
  function fmtWind(mpsx100, u) {
    if (mpsx100 === null || mpsx100 === undefined) return '—';
    var v = windFromMps(mpsx100 / 100, u);
    return u === 'm/s' ? v.toFixed(1) : String(Math.round(v));
  }
  function fmtRainUm(um, u) {
    if (um === null || um === undefined) return '—';
    if (u === 'mm') return (um / 1000).toFixed(1);
    return (um / 25400).toFixed(2);
  }
  function fmtRainMmx10(x10, u) {
    if (u === 'mm') return (x10 / 10).toFixed(1);
    return (x10 / 254).toFixed(2);
  }
  var POINTS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
  function dirName(deg) {
    if (deg === null || deg === undefined) return '—';
    return POINTS[Math.round(deg / 22.5) % 16];
  }

  // ------------------------------------------------------------ time

  // Station-local time, independent of the iPad's own time zone setting.
  function localDate(d, unixSec) {
    return new Date((unixSec + d.tz_off) * 1000);
  }
  function fmtClock(date) {
    var h = date.getUTCHours();
    var mm = date.getUTCMinutes();
    var ap = h < 12 ? 'AM' : 'PM';
    h = h % 12;
    if (h === 0) h = 12;
    return h + ':' + (mm < 10 ? '0' : '') + mm + ' ' + ap;
  }
  var DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  function parseDay(s) {  // "2026-11-12" → Date at UTC midnight
    var p = s.split('-');
    return new Date(Date.UTC(+p[0], +p[1] - 1, +p[2]));
  }

  // ------------------------------------------------------------ weather codes

  function iconKind(code) {
    if (code === null || code === undefined) return 'cloud';
    if (code <= 1) return 'sun';
    if (code === 2) return 'partly';
    if (code === 3) return 'cloud';
    if (code === 45 || code === 48) return 'fog';
    if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 'snow';
    if (code >= 95) return 'storm';
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 'rain';
    return 'cloud';
  }
  var TEXT = {
    0: 'Sunny', 1: 'Mostly sunny', 2: 'Partly cloudy', 3: 'Cloudy', 45: 'Fog', 48: 'Fog',
    51: 'Drizzle', 53: 'Drizzle', 55: 'Drizzle', 56: 'Freezing drizzle', 57: 'Freezing drizzle',
    61: 'Light rain', 63: 'Rain', 65: 'Heavy rain', 66: 'Freezing rain', 67: 'Freezing rain',
    71: 'Light snow', 73: 'Snow', 75: 'Heavy snow', 77: 'Snow', 80: 'Showers', 81: 'Showers',
    82: 'Heavy showers', 85: 'Snow showers', 86: 'Snow showers', 95: 'Thunderstorms',
    96: 'Storms, hail', 99: 'Storms, hail'
  };
  function condText(code, night) {
    var t = TEXT[code] || 'Cloudy';
    if (night && code <= 1) t = 'Clear';
    return t;
  }

  var SUN = '<circle cx="12" cy="12" r="4.2"/><path d="M12 2.5v2.2M12 19.3v2.2M2.5 12h2.2M19.3 12h2.2M5.3 5.3l1.6 1.6M17.1 17.1l1.6 1.6M5.3 18.7l1.6-1.6M17.1 6.9l1.6-1.6"/>';
  var CLOUD = 'M7 18.5h10a4 4 0 0 0 .4-8 5.5 5.5 0 0 0-10.5 1.6A3.2 3.2 0 0 0 7 18.5z';
  var CLOUD_HI = 'M7 14.5h10a4 4 0 0 0 .4-8 5.5 5.5 0 0 0-10.5 1.6A3.2 3.2 0 0 0 7 14.5z';
  var ICONS = {
    sun: '<g class="i-sun">' + SUN + '</g>',
    moon: '<path class="i-sun" d="M19.5 14.6A7.8 7.8 0 0 1 9.4 4.5a7.8 7.8 0 1 0 10.1 10.1z"/>',
    partly: '<g class="i-sun"><circle cx="9" cy="8.5" r="3"/><path d="M9 2.8v1.2M3.3 8.5h1.2M4.9 4.4l.9.9M13.1 4.4l-.9.9"/></g>' +
      '<path class="i-cloud" d="M9 19.5h8.5a3.5 3.5 0 0 0 .3-7 4.8 4.8 0 0 0-9.1 1.4A2.8 2.8 0 0 0 9 19.5z"/>',
    cloud: '<path class="i-cloud" d="' + CLOUD + '"/>',
    fog: '<path class="i-cloud" d="' + CLOUD_HI + '"/><path class="i-cloud" d="M5 18h14M7 21h10"/>',
    rain: '<path class="i-cloud" d="' + CLOUD_HI + '"/><path class="i-rain" d="M8.5 17l-1 3M12.5 17l-1 3M16.5 17l-1 3"/>',
    snow: '<path class="i-cloud" d="' + CLOUD_HI + '"/><path class="i-rain" d="M8 18.5h.01M12 20h.01M16 18.5h.01M10 21.5h.01M14 21.5h.01"/>',
    storm: '<path class="i-cloud" d="' + CLOUD_HI + '"/><path class="i-sun" d="M12.5 15l-2 3.5h3l-2 3.5"/>'
  };
  function iconSvg(kind) {
    return '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.6" stroke-linecap="round" ' +
      'stroke-linejoin="round" aria-hidden="true" width="100%" height="100%">' + ICONS[kind] + '</svg>';
  }

  // ------------------------------------------------------------ DOM helpers

  function all(sel, root) {
    return Array.prototype.slice.call((root || document).querySelectorAll(sel));
  }
  function setText(el, s) {
    if (el.textContent !== s) el.textContent = s;
  }
  function fill(name, s, root) {
    all('[data-f="' + name + '"]', root).forEach(function (el) { setText(el, s); });
  }
  function setIcon(el, kind) {
    if (el.getAttribute('data-kind') === kind) return;
    el.setAttribute('data-kind', kind);
    el.innerHTML = iconSvg(kind);
  }
  function toggleClass(el, cls, on) {
    var has = (' ' + el.className + ' ').indexOf(' ' + cls + ' ') >= 0;
    if (on && !has) el.className = (el.className + ' ' + cls).replace(/^\s+/, '');
    if (!on && has) el.className = (' ' + el.className + ' ').replace(' ' + cls + ' ', ' ').replace(/^\s+|\s+$/g, '');
  }

  // ------------------------------------------------------------ problems

  var PROBLEMS = {
    outdoor_none: function () { return 'No data from the outdoor station yet'; },
    outdoor_stale: function (x) { return 'No data from the outdoor station for ' + x + ' min'; },
    radio: function () { return 'Base station radio not responding'; },
    sensor_th: function () { return 'Outdoor temperature/humidity sensor not responding'; },
    sensor_p: function () { return 'Outdoor pressure sensor not responding'; },
    sensor_wind: function () { return 'Anemometer not reporting'; },
    sensor_dir: function () { return 'Wind vane not reporting'; },
    battery: function (x, l) { return 'Outdoor battery ' + (l > 1 ? 'critical' : 'low') + ' (' + (x / 1000).toFixed(2) + ' V)'; },
    outdoor_restarts: function (x) { return 'Outdoor station restarted itself ' + x + '× in the last day'; },
    base_restarted: function () { return 'Base station restarted itself today'; },
    indoor_sensor: function () { return 'Indoor sensor not responding'; },
    clock: function () { return 'Clock not set yet — rain totals waiting'; },
    forecast: function (x) { return x ? 'Forecast not updated for ' + Math.round(x / 60) + ' h' : 'Forecast not available yet'; },
    no_location: function () { return 'Set the station location on the config page to get a forecast'; },
    flash: function () { return 'Can’t save rain history'; },
    weak_signal: function (x) { return 'Weak signal from outdoor station (' + x + ' dBm)'; }
  };

  function statusBar() {
    var el = document.getElementById('wx-status');
    if (!el) {
      el = document.createElement('a');
      el.id = 'wx-status';
      el.href = '/config#health';
      document.body.appendChild(el);
    }
    return el;
  }

  function showStatus(text, level) {
    var el = statusBar();
    if (!text) {
      el.style.display = 'none';
      return;
    }
    setText(el, text);
    el.className = level > 1 ? 'red' : 'amber';
    el.style.display = 'block';
  }

  function renderProblems(d) {
    if (state.fails >= FAILS_BEFORE_WARNING) {
      var since = state.data ? fmtClock(localDate(state.data, state.data.now)) : 'never';
      showStatus('Can’t reach the weather station — last update ' + since, 2);
      return;
    }
    var ps = (d && d.problems) || [];
    if (!ps.length) {
      showStatus(null);
      return;
    }
    var p = ps[0];
    var f = PROBLEMS[p.c];
    var text = f ? f(p.x, p.l) : p.c;
    if (ps.length > 1) text += '  ·  +' + (ps.length - 1) + ' more';
    showStatus(text, p.l);
  }

  // ------------------------------------------------------------ rendering

  function todayForecast(d) {
    if (!d.fc || !d.fc.days || !d.fc.days.length) return null;
    return d.fc.days[0];
  }

  function isNight(d, fc) {
    if (!d.clock || !fc || fc.sr < 0) return false;
    var t = localDate(d, d.now);
    var min = t.getUTCHours() * 60 + t.getUTCMinutes();
    return min < fc.sr || min >= fc.ss;
  }

  function render(d) {
    var u = d.units;
    var out = d.out || {};
    var inn = d.in || {};
    var fc0 = todayForecast(d);
    var night = isNight(d, fc0);

    fill('out_t', fmtTemp(out.t, u.t));
    fill('out_h', out.h === null || out.h === undefined ? '—' : String(Math.round(out.h / 100)));
    fill('out_p', fmtPress(out.p_sea !== null && out.p_sea !== undefined ? out.p_sea : out.p, u.p));
    fill('in_t', fmtTemp(inn.t, u.t));
    fill('in_h', inn.h === null || inn.h === undefined ? '—' : String(Math.round(inn.h / 100)));
    fill('in_p', fmtPress(inn.p, u.p));
    fill('wind', fmtWind(out.wind, u.w));
    fill('gust', fmtWind(out.gust, u.w));
    fill('dir_name', dirName(out.dir));
    fill('dir_deg', out.dir === null || out.dir === undefined ? '' : out.dir + '°');
    fill('hi', fmtTemp(out.hi, u.t));
    fill('lo', fmtTemp(out.lo, u.t));
    fill('u_p', u.p);
    fill('u_w', u.w);
    fill('u_r', u.r);
    fill('u_t', '°' + u.t);
    fill('p_label', out.p_sea !== null && out.p_sea !== undefined ? 'Pressure' : 'Pressure (station)');

    var r = d.rain || {};
    fill('rain_today', fmtRainUm(r.today_um, u.r));
    fill('rain_month', fmtRainUm(r.month_um, u.r));
    fill('rain_year', fmtRainUm(r.year_um, u.r));

    // Updated time + staleness color.
    var age = out.age_s;
    var upd = '—';
    if (d.clock && age !== undefined && age !== null) upd = 'Updated ' + fmtClock(localDate(d, d.now - age));
    else if (age !== undefined && age !== null) upd = 'Updated ' + Math.round(age / 60) + ' min ago';
    else upd = 'Waiting for outdoor station';
    all('[data-f="updated"]').forEach(function (el) {
      setText(el, upd);
      toggleClass(el, 'stale-amber', age >= 300 && age < 900);
      toggleClass(el, 'stale-red', age === undefined || age === null || age >= 900);
    });

    // Current conditions (from the forecast; the station can't see clouds).
    var code = d.fc ? d.fc.code : null;
    fill('cond', d.fc ? condText(code, night) : 'Forecast loading…');
    var nowKind = d.fc ? iconKind(code) : 'cloud';
    if (night && nowKind === 'sun') nowKind = 'moon';
    if (night && nowKind === 'partly') nowKind = 'cloud';
    all('[data-icon="now"]').forEach(function (el) { setIcon(el, nowKind); });

    fill('fc_hi', fc0 ? fmtTemp(fc0.hi * 10, u.t) : '—');
    fill('fc_lo', fc0 ? fmtTemp(fc0.lo * 10, u.t) : '—');
    fill('fc_cond', fc0 ? condText(fc0.c, false) : '—');
    all('[data-icon="today"]').forEach(function (el) { setIcon(el, fc0 ? iconKind(fc0.c) : 'cloud'); });
    fill('fc_age', d.fc ? (d.fc.age_s > 10800 ? 'Forecast ' + Math.round(d.fc.age_s / 3600) + ' h old' : '') : '');

    // Wind compass.
    all('[data-rot="dir"]').forEach(function (el) {
      var c = el.getAttribute('data-c') || '100 100';
      var deg = out.dir === null || out.dir === undefined ? 0 : out.dir;
      el.setAttribute('transform', 'rotate(' + deg + ' ' + c + ')');
      el.style.display = out.dir === null || out.dir === undefined ? 'none' : '';
    });
    all('[data-rot="toward"]').forEach(function (el) {  // arrow pointing where the wind blows to
      var c = el.getAttribute('data-c') || '12 12';
      var deg = out.dir === null || out.dir === undefined ? 0 : (out.dir + 180) % 360;
      el.setAttribute('transform', 'rotate(' + deg + ' ' + c + ')');
    });

    renderForecastLists(d, u);
    renderTodayRange(d, u, fc0);
  }

  function renderForecastLists(d, u) {
    var days = (d.fc && d.fc.days) || [];
    var lo = Infinity, hi = -Infinity, i;
    for (i = 0; i < days.length; i++) {
      if (days[i].lo < lo) lo = days[i].lo;
      if (days[i].hi > hi) hi = days[i].hi;
    }
    var span = hi - lo || 1;

    all('[data-list="fc"]').forEach(function (list) {
      var skip = +(list.getAttribute('data-skip') || 0);  // 1 = start tomorrow
      var max = +(list.getAttribute('data-n') || 5);
      if (!list._tpl) {
        list._tpl = list.children[0];
        list.removeChild(list._tpl);
      }
      var rows = days.slice(skip, skip + max);
      while (list.children.length < rows.length) list.appendChild(list._tpl.cloneNode(true));
      while (list.children.length > rows.length) list.removeChild(list.lastChild);
      rows.forEach(function (day, idx) {
        var row = list.children[idx];
        var date = parseDay(day.d);
        var name = (idx === 0 && skip === 0) ? 'Today' : DAYS[date.getUTCDay()];
        fill('name', name, row);
        all('[data-k]', row).forEach(function (el) {
          var k = el.getAttribute('data-k');
          var s = '';
          if (k === 'name') s = name;
          else if (k === 'date') s = MONTHS[date.getUTCMonth()] + ' ' + date.getUTCDate();
          else if (k === 'cond') s = condText(day.c, false);
          else if (k === 'hi') s = fmtTemp(day.hi * 10, u.t);
          else if (k === 'lo') s = fmtTemp(day.lo * 10, u.t);
          else if (k === 'pp') s = day.pp ? day.pp + '%' : '';
          else if (k === 'pr') s = day.pr ? fmtRainMmx10(day.pr, u.r) + ' ' + u.r : '';
          setText(el, s);
        });
        all('[data-icon="day"]', row).forEach(function (el) { setIcon(el, iconKind(day.c)); });
        all('[data-bar]', row).forEach(function (el) {
          var a = (day.lo - lo) / span * 100, b = (day.hi - day.lo) / span * 100;
          if (el.getAttribute('data-bar') === 'v') {
            el.style.top = (100 - a - b) + '%';
            el.style.height = Math.max(b, 4) + '%';
          } else {
            el.style.left = a + '%';
            el.style.width = Math.max(b, 4) + '%';
          }
        });
        toggleClass(row, 'first', idx === 0);
      });
    });
  }

  function renderTodayRange(d, u, fc0) {
    var box = document.querySelector('[data-range="today"]');
    if (!box) return;
    var out = d.out || {};
    var pts = [];
    if (out.lo !== null && out.lo !== undefined) pts.push(out.lo / 10, out.hi / 10);
    if (fc0) pts.push(fc0.lo, fc0.hi);
    if (out.t !== null && out.t !== undefined) pts.push(out.t / 10);
    if (!pts.length) { box.style.visibility = 'hidden'; return; }
    box.style.visibility = '';
    var min = Math.min.apply(null, pts) - 10, max = Math.max.apply(null, pts) + 10;  // x10 °C, padded
    function pos(x10) { return ((x10 - min) / (max - min) * 100) + '%'; }
    function place(sel, x10, width) {
      var el = box.querySelector(sel);
      if (!el) return;
      if (x10 === null || x10 === undefined) { el.style.display = 'none'; return; }
      el.style.display = '';
      el.style.left = pos(x10);
      if (width !== undefined) el.style.width = width;
    }
    if (out.lo !== null && out.lo !== undefined) {
      place('.r-actual', out.lo / 10, ((out.hi - out.lo) / 10 / (max - min) * 100) + '%');
    } else {
      place('.r-actual', null);
    }
    place('.r-fclo', fc0 ? fc0.lo : null);
    place('.r-fchi', fc0 ? fc0.hi : null);
    place('.r-now', out.t === null || out.t === undefined ? null : out.t / 10);
    fill('now_t', fmtTemp(out.t, u.t));
  }

  // ------------------------------------------------------------ polling

  function poll() {
    var x = new XMLHttpRequest();
    var done = false;
    x.open('GET', '/data.json?t=' + new Date().getTime(), true);
    x.timeout = 8000;
    x.onreadystatechange = function () {
      if (x.readyState !== 4 || done) return;
      done = true;
      if (x.status === 200) {
        var d = null;
        try { d = JSON.parse(x.responseText); } catch (e) { d = null; }
        if (d) {
          state.fails = 0;
          state.lastOk = new Date().getTime();
          // Layout follows the setting (or day/night) unless overridden in the URL.
          if (!state.override && d.layout && window.WX_LAYOUT && d.layout !== window.WX_LAYOUT) {
            location.reload();
            return;
          }
          state.data = d;
          try { render(d); } catch (e2) { /* keep the last good screen */ }
          renderProblems(d);
          return;
        }
      }
      state.fails++;
      renderProblems(state.data);
    };
    x.ontimeout = function () {
      if (done) return;
      done = true;
      state.fails++;
      renderProblems(state.data);
    };
    x.send();
  }

  // Exposed for the config page and tests.
  window.WX = {
    fmtTemp: fmtTemp, fmtPress: fmtPress, fmtWind: fmtWind, fmtRainUm: fmtRainUm,
    dirName: dirName, iconKind: iconKind, condText: condText, render: render
  };

  // Scale the fixed 1024×768 design to whatever the window is (Safari with
  // or without its bars, portrait, a phone).
  function fit() {
    var root = document.getElementById('wx-root');
    if (!root) return;
    var s = Math.min(window.innerWidth / 1024, window.innerHeight / 768);
    var t = 'scale(' + s + ')';
    root.style.webkitTransform = t;
    root.style.transform = t;
    root.style.left = Math.max(0, (window.innerWidth - 1024 * s) / 2) + 'px';
  }

  function injectStyle() {
    var css =
      '#wx-status{position:fixed;left:0;right:0;bottom:0;height:34px;line-height:34px;padding:0 18px;' +
      'font:600 16px "Helvetica Neue",Helvetica,Arial,sans-serif;color:#111;text-decoration:none;' +
      'display:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;z-index:10}' +
      '#wx-status.amber{background:#F2C66D}#wx-status.red{background:#F07B6B}' +
      '.stale-amber{color:#E8A93B !important}.stale-red{color:#F07B6B !important}' +
      '[data-icon]{display:inline-block}';
    var st = document.createElement('style');
    st.appendChild(document.createTextNode(css));
    document.head.appendChild(st);
  }

  if (window.WX_LAYOUT) {
    injectStyle();
    fit();
    window.addEventListener('resize', fit);
    window.addEventListener('orientationchange', function () { setTimeout(fit, 300); });
    poll();
    setInterval(poll, POLL_MS);
    // Safety net: reload the page every 6 hours so a long-running Safari
    // never drifts (memory, a stuck timer). Harmless on a wall display.
    setTimeout(function () { location.reload(); }, 6 * 3600 * 1000);
  }
})();
