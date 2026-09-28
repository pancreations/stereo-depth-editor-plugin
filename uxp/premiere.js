const ppro = require("premierepro");
const MATCH = "StereoDepth.2Dto3D";

async function selected() {
  const project = await ppro.Project.getActiveProject();
  if (!project) throw new Error("Open a Premiere project.");
  const sequence = await project.getActiveSequence();
  if (!sequence) throw new Error("Open a sequence and select one video clip.");
  const selection = await sequence.getSelection();
  const items = await selection.getTrackItems();
  const video = [];
  for (const item of items) {
    // Audio track items do not expose this video-specific method.
    if (typeof item.isAdjustmentLayer === "function") video.push(item);
  }
  if (video.length !== 1) throw new Error("Select exactly one video clip.");
  if (await video[0].isSpeedReversed()) throw new Error("Reverse/time-remapped clips need a rendered CFR source before caching.");
  const speed = await video[0].getSpeed();
  if (speed !== 1) throw new Error("Use a 100% speed clip; render time changes before caching.");
  return { project, clip: video[0] };
}

async function sourcePath() {
  const { clip } = await selected();
  const item = ppro.ClipProjectItem.cast(await clip.getProjectItem());
  if (await item.isSequence()) throw new Error("For a nested Full-SBS canvas, browse the original source manually.");
  return item.getMediaFilePath();
}

async function apply(slot, external) {
  if (!Number.isInteger(slot) || slot < 1 || slot > 999999) throw new Error("Enter a valid cache slot.");
  const { project, clip } = await selected();
  const names = await ppro.VideoFilterFactory.getMatchNames();
  const registeredName = names.find(name => name === MATCH || name === "AE." + MATCH);
  if (!registeredName) throw new Error("The native 2D to 3D effect is not installed. Build/install StereoDepth.prm with the Adobe SDKs first.");
  const chain = await clip.getComponentChain();
  let effect;
  for (let i = 0; i < chain.getComponentCount(); i++) {
    const component = chain.getComponentAtIndex(i);
    if (await component.getMatchName() === registeredName) { effect = component; break; }
  }
  if (!effect) {
    const filter = await ppro.VideoFilterFactory.createComponent(registeredName);
    if (!project.executeTransaction(compound => compound.addAction(chain.createAppendComponentAction(filter)), "Apply 2D to 3D")) {
      throw new Error("Premiere could not apply the effect.");
    }
    effect = chain.getComponentAtIndex(chain.getComponentCount() - 1);
  }
  const changes = [];
  for (let i = 0; i < effect.getParamCount(); i++) {
    const param = effect.getParam(i);
    if (param.displayName === "Depth Cache Slot") changes.push(param.createSetValueAction(param.createKeyframe(slot), true));
    if (param.displayName === "Depth Pass Override") changes.push(param.createSetValueAction(param.createKeyframe(external), true));
  }
  if (changes.length !== 2) throw new Error("Native effect parameter version does not match this panel.");
  if (!project.executeTransaction(compound => changes.forEach(action => compound.addAction(action)), "Bind StereoDepth Cache")) {
    throw new Error("Premiere could not bind the depth cache.");
  }
}

module.exports = { sourcePath, apply };
