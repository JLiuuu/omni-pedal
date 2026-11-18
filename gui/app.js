const EFFECTS = [
  ["bypass", "Bypass"],
  ["overdrive", "Overdrive"],
  ["distortion", "Distortion"],
  ["delay", "Delay"],
  ["chorus", "Chorus"],
  ["reverb", "Reverb"],
  ["bass", "Bass Split"],
];

const KNOBS = [
  ["drive", "Drive"],
  ["tone", "Tone"],
  ["mix", "Mix"],
  ["time", "Time"],
  ["level", "Level"],
];

const USED = {
  bypass: { level: true },
  overdrive: { drive: true, tone: true, mix: true, level: true },
  distortion: { drive: true, tone: true, mix: true, level: true },
  delay: { drive: true, mix: true, time: true, level: true },
  chorus: { tone: true, mix: true, time: true, level: true },
  reverb: { tone: true, mix: true, time: true, level: true },
  bass: { tone: true, mix: true, time: true, level: true },
};

const HINTS = {
  bypass: { level: "Output level. The guitar stays dry." },
  overdrive: {
    drive: "How hard the clipper is pushed.",
    tone: "Brightness after the clip.",
    mix: "Dry guitar to overdrive.",
    level: "Output level.",
  },
  distortion: {
    drive: "Distortion amount.",
    tone: "Brightness after the distortion.",
    mix: "Dry guitar to distortion.",
    level: "Output level.",
  },
  delay: {
    drive: "Echo feedback.",
    mix: "Echo loudness.",
    time: "Delay time, about 40 ms to 800 ms.",
    level: "Output level.",
  },
  chorus: {
    tone: "How far the pitch wobbles.",
    mix: "Chorus loudness.",
    time: "Wobble speed.",
    level: "Output level.",
  },
  reverb: {
    tone: "Darker to brighter tail.",
    mix: "Reverb loudness.",
    time: "Room size.",
    level: "Output level.",
  },
  bass: {
    tone: "Tone of the added octave.",
    mix: "Octave loudness.",
    time: "Split, about 80 Hz to 250 Hz. Notes below it get the octave.",
    level: "Output level.",
  },
};

const state = {
  connected: false,
  effect: "bypass",
  drive: 0.35,
  tone: 0.65,
  mix: 0.45,
  time: 0.35,
  level: 1.0,
};

let dragging = null;
let sendTimer = 0;

const effectRow = document.getElementById("effects");
const knobColumn = document.getElementById("knobs");

function effectLabel(name) {
  for (const pair of EFFECTS) {
    if (pair[0] === name) {
      return pair[1];
    }
  }
  return name;
}

function formatValue(effect, key, value) {
  const percent = Math.round(value * 100) + "%";
  if (key === "time" && effect === "delay") {
    return Math.round(40 + value * 760) + " ms";
  }
  if (key === "time" && effect === "chorus") {
    return (0.3 + value * 3.2).toFixed(2) + " Hz";
  }
  if (key === "time" && effect === "bass") {
    return Math.round(80 + value * 170) + " Hz split";
  }
  if (key === "tone" && effect === "bass") {
    return Math.round(90 + value * 150) + " Hz";
  }
  if (key === "drive" && effect === "delay") {
    return Math.round((0.05 + value * 0.67) * 100) + "% feedback";
  }
  return percent;
}

function send(line, immediate) {
  const post = () => {
    fetch("/api/command", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ line: line }),
    }).catch(() => {});
  };
  if (immediate) {
    clearTimeout(sendTimer);
    post();
    return;
  }
  clearTimeout(sendTimer);
  sendTimer = setTimeout(post, 40);
}

function render() {
  document.getElementById("effect-name").textContent = effectLabel(state.effect);
  document.getElementById("link").textContent = state.connected ? "Audio engine connected" : "Preview — start stage1 --panel";
  document.getElementById("led").className = state.connected ? "led on" : "led";
  for (const button of effectRow.querySelectorAll("button")) {
    button.classList.toggle("selected", button.dataset.effect === state.effect);
  }
  for (const row of knobColumn.querySelectorAll(".knob")) {
    const key = row.dataset.key;
    const used = USED[state.effect] && USED[state.effect][key];
    row.classList.toggle("idle", !used);
    row.querySelector(".value").textContent = formatValue(state.effect, key, state[key]);
    const hint = (HINTS[state.effect] && HINTS[state.effect][key]) || "Kept for the next effect. Not used by this one.";
    row.querySelector(".hint").textContent = hint;
    const pct = Math.round(state[key] * 1000) / 10;
    row.querySelector(".track").setAttribute("aria-valuenow", String(Math.round(state[key] * 100)));
    row.querySelector(".track").setAttribute("aria-valuemin", "0");
    row.querySelector(".track").setAttribute("aria-valuemax", "100");
    if (dragging !== key) {
      row.querySelector(".fill").style.width = pct + "%";
      row.querySelector(".thumb").style.left = pct + "%";
    }
  }
}

function bindSlider(row) {
  const key = row.dataset.key;
  const track = row.querySelector(".track");
  track.setAttribute("role", "slider");
  track.setAttribute("aria-label", row.querySelector("strong").textContent);
  track.tabIndex = 0;
  const move = (event) => {
    const rect = track.getBoundingClientRect();
    let value = (event.clientX - rect.left) / rect.width;
    if (value < 0) {
      value = 0;
    }
    if (value > 1) {
      value = 1;
    }
    state[key] = value;
    const pct = Math.round(value * 1000) / 10;
    row.querySelector(".fill").style.width = pct + "%";
    row.querySelector(".thumb").style.left = pct + "%";
    row.querySelector(".value").textContent = formatValue(state.effect, key, value);
    send(key + " " + value.toFixed(3), false);
  };
  track.addEventListener("pointerdown", (event) => {
    dragging = key;
    track.setPointerCapture(event.pointerId);
    move(event);
  });
  track.addEventListener("pointermove", (event) => {
    if (dragging !== key) {
      return;
    }
    move(event);
  });
  const finish = () => {
    if (dragging === key) {
      send(key + " " + state[key].toFixed(3), true);
      dragging = null;
    }
  };
  track.addEventListener("pointerup", finish);
  track.addEventListener("pointercancel", finish);
}

for (const pair of EFFECTS) {
  const button = document.createElement("button");
  button.type = "button";
  button.textContent = pair[1];
  button.dataset.effect = pair[0];
  button.addEventListener("click", () => {
    state.effect = pair[0];
    render();
    send("effect " + pair[0], true);
  });
  effectRow.appendChild(button);
}

for (const pair of KNOBS) {
  const row = document.createElement("div");
  row.className = "knob";
  row.dataset.key = pair[0];
  row.innerHTML =
    '<div class="knob-top"><strong></strong><span class="value"></span></div>' +
    '<div class="track"><div class="fill"></div><div class="thumb"></div></div>' +
    '<p class="hint"></p>';
  row.querySelector("strong").textContent = pair[1];
  bindSlider(row);
  knobColumn.appendChild(row);
}

document.getElementById("next").addEventListener("click", () => {
  const index = EFFECTS.findIndex((pair) => pair[0] === state.effect);
  state.effect = EFFECTS[(index + 1) % EFFECTS.length][0];
  render();
  send("next", true);
});

document.getElementById("fill").addEventListener("click", () => {
  if (!document.fullscreenElement) {
    document.documentElement.requestFullscreen().catch(() => {});
  } else {
    document.exitFullscreen();
  }
});

async function poll() {
  try {
    const response = await fetch("/api/state");
    const data = await response.json();
    state.connected = !!data.connected;
    if (!dragging) {
      state.effect = data.effect || state.effect;
      for (const pair of KNOBS) {
        if (typeof data[pair[0]] === "number") {
          state[pair[0]] = data[pair[0]];
        }
      }
    }
    render();
  } catch (error) {
    state.connected = false;
    render();
  }
}

render();
poll();
setInterval(poll, 400);
