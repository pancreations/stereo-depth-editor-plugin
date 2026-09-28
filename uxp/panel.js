const { storage, entrypoints } = require("uxp");
const premiere = require("./premiere.js");
const fs = storage.localFileSystem;
const $ = id => document.getElementById(id);
let connection = null, job = null, completed = null, timer = null, visible = true;
let modelRows = [], modelTimer = null, modelBusy = false, modelsLoaded = false;
function showTab(cache) {
  for (const id of ["cache-inputs", "temporal-section", "quality-section", "cache-section"]) $(id).className = cache ? "" : "hidden";
  $("show-models").className = cache ? "" : "active";
  $("show-cache").className = cache ? "active" : "";
}
$("show-models").addEventListener("click", () => showTab(false));
$("show-cache").addEventListener("click", () => showTab(true));

function status(text, error = false) { $("status").textContent = text; $("status").className = error ? "error" : ""; }
function on(id, handler) {
  $(id).addEventListener("click", async () => {
    $(id).disabled = true;
    try { await handler(); } catch (error) { status(error.message, true); }
    finally {
      $(id).disabled = false;
      if (id === "generate" && job) $(id).disabled = true;
      if (id === "download-model") selectModel();
    }
  });
}
async function browse(id, types) {
  const file = await fs.getFileForOpening({ types });
  if (file) $(id).value = file.nativePath;
  return !!file;
}
async function api(path, body) {
  if (!connection) throw new Error("Connect the local worker first.");
  const response = await fetch(connection.url + "/v1" + path, {
    method: body ? "POST" : "GET",
    headers: { Authorization: "Bearer " + connection.token, "Content-Type": "application/json" },
    ...(body ? { body: JSON.stringify(body) } : {})
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || "Worker request failed.");
  return data;
}
function selectModel() {
  const row = modelRows.find(item => item.id === $("model-choice").value);
  if (row) {
    $("model").value = row.path;
    $("model-info").textContent = row.name + " | " + row.license + " | " + (row.installed ? "Installed" : "Not installed") +
      (row.family === "da3" ? " | Single-view depth; optional flow stabilization" : "") + " | " + row.validation;
  } else {
    $("model-info").textContent = "Custom model manifest. Choose a registered local model.";
  }
  $("download-model").disabled = modelBusy || !row || !row.downloadable || row.installed;
}
function scheduleModelPoll() {
  if (modelTimer) clearTimeout(modelTimer);
  if (visible && connection && modelBusy) modelTimer = setTimeout(() => refreshModels().catch(error => {
    $("model-status").textContent = error.message + " Use Refresh Models to reconnect.";
  }), 1000);
}
async function refreshModels() {
  const data = await api("/models");
  const previous = modelsLoaded ? $("model-choice").value : ($("model").value.trim() ? "custom" : data.default);
  const previousPath = $("model").value;
  modelRows = data.models;
  const select = $("model-choice");
  while (select.firstChild) select.removeChild(select.firstChild);
  for (const row of modelRows) {
    const option = document.createElement("option");
    option.value = row.id;
    option.textContent = row.name + (row.installed ? " (installed)" : " (setup needed)");
    select.appendChild(option);
  }
  const custom = document.createElement("option");
  custom.value = "custom"; custom.textContent = "Custom local manifest"; select.appendChild(custom);
  select.value = modelRows.some(row => row.id === previous) ? previous : "custom";
  modelsLoaded = true;
  const state = data.download;
  modelBusy = ["downloading", "registering"].includes(state.state);
  $("model-status").textContent = state.state === "idle" ? "Choose any model size above. Downloads use the official model repositories." :
    (state.id || "Model") + ": " + state.state + (state.bytes ? " | " + Math.round(state.bytes / 1048576) + " MB" +
      (state.total_bytes ? " / " + Math.round(state.total_bytes / 1048576) + " MB" : "") : "") + (state.error ? " | " + state.error : "");
  selectModel();
  if (select.value === "custom") $("model").value = previousPath;
  scheduleModelPoll();
}
$("model-choice").addEventListener("change", selectModel);
$("model").addEventListener("input", () => { $("model-choice").value = "custom"; selectModel(); });
on("refresh-models", refreshModels);
on("download-model", async () => {
  await api("/models/download", { id: $("model-choice").value });
  await refreshModels();
});
function schedulePoll() {
  if (timer) clearTimeout(timer);
  if (visible && job) timer = setTimeout(poll, 750);
}
async function poll() {
  if (!job) return;
  try {
    const state = await api("/jobs/" + job);
    status(state.state + (state.completed ? ": " + state.completed + " frames" : "") + (state.error ? " — " + state.error : ""), state.state === "failed");
    $("progress").value = state.total ? Math.round(100 * state.completed / state.total) : 0;
    if (["complete", "failed", "cancelled"].includes(state.state)) {
      if (state.state === "complete") {
        completed = state; $("slot").value = state.slot; $("clear").disabled = false;
        status("Ready · " + state.completed + " frames · cache slot " + state.slot + ". Apply / Bind to use it.");
      }
      job = null; $("generate").disabled = false; $("cancel").disabled = true;
    }
  } catch (error) { status(error.message + " Reconnect the worker to resume status checks.", true); return; }
  schedulePoll();
}
on("connect", async () => {
  const file = await fs.getFileForOpening({ types: ["json"] });
  if (!file) return;
  const data = JSON.parse(await file.read());
  if (data.url !== "http://127.0.0.1:48731" || typeof data.token !== "string" || data.token.length < 32) {
    throw new Error("Invalid local worker connection file.");
  }
  connection = data;
  const info = await api("/status");
  $("open-preview").disabled = info.capabilities.sbs_preview === false;
  $("connection").textContent = "Local worker connected · " + info.version;
  $("cache-root").textContent = "Cache location: " + info.cache_root;
  job = info.active; schedulePoll(); await refreshModels();
});
on("open-preview", async () => {
  await api("/preview/open", {});
  status("SBS Preview opened. Capture the window named StereoDepth SBS Preview. Right-click it for window controls.");
});
on("selected", async () => { $("source").value = await premiere.sourcePath(); });
on("browse-source", () => browse("source", ["*"]));
on("browse-depth", () => browse("depth-source", ["png", "exr", "tif", "tiff", "pfm", "mov", "mp4", "mkv", "avi"]));
on("browse-model", async () => {
  if (await browse("model", ["json"])) { $("model-choice").value = "custom"; selectModel(); }
});
on("generate", async () => {
  const value = id => $(id).value.trim();
  const numeric = id => { const v = Number(value(id)); if (!Number.isFinite(v)) throw new Error("Invalid number: " + id); return v; };
  if (!value("source")) throw new Error("Choose source footage first.");
  const selectedModel = modelRows.find(row => row.id === $("model-choice").value);
  if (!$("override").checked && selectedModel && !selectedModel.installed) throw new Error("Download or register the selected model first, then refresh models.");
  const request = {
    source: value("source"), depth_override: $("override").checked,
    depth_source: value("depth-source"), model: value("model"), fps: value("fps") || null,
    quality: value("quality"), device: value("device"), channel: value("channel"),
    depth_frame_offset: numeric("depth-offset"),
    temporal: { enabled: $("temporal").checked, strength: numeric("temporal-strength"),
                motion_sensitivity: numeric("motion"), edge_preservation: numeric("temporal-edge") }
  };
  job = (await api("/jobs", request)).id;
  completed = null; $("clear").disabled = true; $("cancel").disabled = false;
  status("Depth job queued."); schedulePoll();
});
on("cancel", async () => { if (job) await api("/cancel", { id: job }); });
on("apply", async () => {
  await premiere.apply(Number($("slot").value), completed ? completed.external : $("override").checked);
  status("Effect bound. Verify Depth Map preview and frame alignment, then adjust stereo in Effect Controls.");
});
on("clear", async () => {
  if (!completed) throw new Error("No completed cache selected.");
  await api("/cache/clear", { key: completed.key });
  completed = null; status("Current depth cache cleared. Generate it again before rendering.");
});
entrypoints.setup({ panels: { stereodepth: {
  show() { visible = true; schedulePoll(); scheduleModelPoll(); },
  hide() { visible = false; if (timer) clearTimeout(timer); if (modelTimer) clearTimeout(modelTimer); },
  destroy() { visible = false; if (timer) clearTimeout(timer); if (modelTimer) clearTimeout(modelTimer); connection = null; }
} } });
