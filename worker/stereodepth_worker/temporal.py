from dataclasses import dataclass

import cv2
import numpy as np


@dataclass(frozen=True)
class Temporal:
    enabled: bool = False
    strength: float = 0.6
    motion_sensitivity: float = 0.7
    edge_preservation: float = 0.8

    def __post_init__(self):
        if not all(np.isfinite(v) and 0 <= v <= 1 for v in (self.strength, self.motion_sensitivity, self.edge_preservation)):
            raise ValueError("Temporal controls must be between zero and one")


def stabilize(current, previous, rgb, previous_rgb, settings: Temporal):
    """One raw predecessor, bidirectional flow rejection, no recursive temporal lag."""
    if not settings.enabled or previous is None:
        return current.copy()
    gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
    old_gray = cv2.cvtColor(previous_rgb, cv2.COLOR_RGB2GRAY)
    if gray.shape != old_gray.shape or previous.shape != current.shape:
        raise ValueError("Temporal dimensions changed")
    if np.mean(np.abs(gray.astype(np.float32) - old_gray)) > 55:
        return current.copy()
    h, w = gray.shape
    size = (max(1, round(w * min(1, 640 / max(w, h)))), max(1, round(h * min(1, 640 / max(w, h)))))
    a, b = cv2.resize(gray, size), cv2.resize(old_gray, size)
    args = (None, .5, 3, 15, 3, 5, 1.2, 0)
    backward = cv2.calcOpticalFlowFarneback(a, b, *args)
    forward = cv2.calcOpticalFlowFarneback(b, a, *args)
    backward = cv2.resize(backward, (w, h)) * np.array([w / size[0], h / size[1]], dtype=np.float32)
    forward = cv2.resize(forward, (w, h)) * np.array([w / size[0], h / size[1]], dtype=np.float32)
    yy, xx = np.mgrid[:h, :w].astype(np.float32)
    mx, my = xx + backward[..., 0], yy + backward[..., 1]
    warp = lambda data: cv2.remap(data, mx, my, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    aligned = warp(previous)
    photo = np.abs(gray.astype(np.float32) - warp(old_gray).astype(np.float32)) / 255
    flow_error = np.linalg.norm(backward + warp(forward), axis=2)
    gradient = np.abs(cv2.Sobel(current, cv2.CV_32F, 1, 0)) + np.abs(cv2.Sobel(current, cv2.CV_32F, 0, 1))
    valid = (mx >= 0) & (mx < w - 1) & (my >= 0) & (my < h - 1)
    weight = min(.85, settings.strength) * valid * np.exp(-photo * (10 + settings.motion_sensitivity * 90))
    weight *= np.exp(-flow_error * 2 - gradient * settings.edge_preservation * 20)
    weight *= np.exp(-np.abs(current - aligned) * 12)
    return (current * (1 - weight) + aligned * weight).astype(np.float32)
