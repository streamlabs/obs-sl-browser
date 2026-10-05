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
	[DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	[DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
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
	public static bool Close(long h) { return PostMessage(new IntPtr(h), 0x0010, IntPtr.Zero, IntPtr.Zero); }
}
"@
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$all = [SltWindows]::All()
if ($Action -eq 'close') {
	$hit = $all | Where-Object { $_.Value -eq $Title } | Select-Object -First 1
	if ($hit) { [void][SltWindows]::Close($hit.Key) }
	ConvertTo-Json -Compress -InputObject @{ closed = [bool]$hit }
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
