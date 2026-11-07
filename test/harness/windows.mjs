/*
 * Top-level windows, as the OS sees them.
 *
 * Window titles and the user closing a window are not visible from any page, so the tab
 * tests ask Windows directly: list the titles, or post the WM_CLOSE a user clicking the
 * close button would cause. Windows PowerShell, so nothing needs installing.
 */

import { execFileSync } from "node:child_process";
import { writeFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";

const SCRIPT = `
param([string]$Action, [string]$Title)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class SltWindows {
	delegate bool EnumProc(IntPtr h, IntPtr l);
	[StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
	[DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	[DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out Rect r);
	[DllImport("user32.dll", SetLastError = true)] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
	[DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
	public static List<KeyValuePair<long, string>> All() {
		var list = new List<KeyValuePair<long, string>>();
		EnumWindows((h, l) => {
			var sb = new StringBuilder(512);
			GetWindowText(h, sb, sb.Capacity);
			if (sb.Length > 0) list.Add(new KeyValuePair<long, string>(h.ToInt64(), sb.ToString()));
			return true;
		}, IntPtr.Zero);
		return list;
	}
	public static int[] ClientSize(long h) {
		// A DPI-unaware PowerShell caller would receive virtualized logical pixels.
		IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
		if (previous == IntPtr.Zero) throw new Exception("cannot set per-monitor DPI awareness: " + Marshal.GetLastWin32Error());
		try {
			Rect r;
			if (!GetClientRect(new IntPtr(h), out r)) return new int[] { 0, 0 };
			return new int[] { r.Right - r.Left, r.Bottom - r.Top };
		} finally {
			if (SetThreadDpiAwarenessContext(previous) == IntPtr.Zero)
				throw new Exception("cannot restore DPI awareness: " + Marshal.GetLastWin32Error());
		}
	}
	public static bool Close(long h) { return PostMessage(new IntPtr(h), 0x0010, IntPtr.Zero, IntPtr.Zero); }
}
"@
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$all = [SltWindows]::All()
if ($Action -eq 'close') {
	$hit = $all | Where-Object { $_.Value -eq $Title } | Select-Object -First 1
	if ($hit) { [void][SltWindows]::Close($hit.Key) }
	ConvertTo-Json -Compress -InputObject @{ closed = [bool]$hit }
} elseif ($Action -eq 'client-size') {
	$hit = $all | Where-Object { $_.Value -eq $Title } | Select-Object -First 1
	$size = if ($hit) { [SltWindows]::ClientSize($hit.Key) } else { @(0, 0) }
	ConvertTo-Json -Compress -InputObject @{ width = $size[0]; height = $size[1] }
} elseif ($Action -eq 'state') {
	$hit = $all | Where-Object { $_.Value -eq $Title } | Select-Object -First 1
	$h = if ($hit) { [IntPtr]::new($hit.Key) } else { [IntPtr]::Zero }
	ConvertTo-Json -Compress -InputObject @{ found = [bool]$hit; visible = [bool]($hit -and [SltWindows]::IsWindowVisible($h)); minimized = [bool]($hit -and [SltWindows]::IsIconic($h)) }
} else {
	ConvertTo-Json -Compress -InputObject @($all | ForEach-Object { $_.Value })
}
`;

let scriptPath = null;

function run(workDir, args) {
	if (!scriptPath) {
		mkdirSync(workDir, { recursive: true });
		scriptPath = join(workDir, "windows.ps1");
		writeFileSync(scriptPath, SCRIPT);
	}
	const out = execFileSync("powershell.exe",
		["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", scriptPath, ...args],
		{ encoding: "utf8", timeout: 30000 });
	return JSON.parse(out.trim());
}

/** The titles of every top-level window that has one. */
export function windowTitles(workDir) {
	const r = run(workDir, ["list", ""]);
	return Array.isArray(r) ? r : [r];
}

/** Post WM_CLOSE to the first window with exactly this title. True if there was one. */
export function closeWindow(workDir, title) {
	return run(workDir, ["close", title]).closed;
}

/** {found, visible, minimized} for the first window with exactly this title. Hidden windows are found too. */
export function windowState(workDir, title) {
	return run(workDir, ["state", title]);
}

/** Native client dimensions, in physical pixels, for a top-level window. */
export function clientSize(workDir, title) {
	return run(workDir, ["client-size", title]);
}
