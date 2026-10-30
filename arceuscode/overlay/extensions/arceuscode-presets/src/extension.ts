import * as vscode from 'vscode';
import { DEFAULT_PRESETS, DEFAULT_COMMANDS, Preset, FrameworkCommand } from './defaults';

const CFG = 'arceusCode';

function allPresets(): Preset[] {
	const user = vscode.workspace.getConfiguration(CFG).get<Preset[]>('presets.library') ?? [];
	const byId = new Map<string, Preset>();
	for (const p of DEFAULT_PRESETS) byId.set(p.id, p);
	for (const p of user) if (p && p.id) byId.set(p.id, p);
	return [...byId.values()];
}

function allCommands(): FrameworkCommand[] {
	const user = vscode.workspace.getConfiguration(CFG).get<FrameworkCommand[]>('commands.library') ?? [];
	const byId = new Map<string, FrameworkCommand>();
	for (const c of DEFAULT_COMMANDS) byId.set(c.id, c);
	for (const c of user) if (c && c.id) byId.set(c.id, c);
	return [...byId.values()];
}

async function pickPreset() {
	const presets = allPresets();
	const active = vscode.workspace.getConfiguration(CFG).get<string>('presets.active') ?? 'general';
	const items: vscode.QuickPickItem[] = presets.map(p => ({
		label: `${p.icon ?? ''} ${p.name}`.trim(),
		description: p.id === active ? '✓ current' : p.id,
		detail: p.description
	}));
	const chosen = await vscode.window.showQuickPick(items, {
		placeHolder: 'Pick a preset for the Arceus agent',
		matchOnDescription: true,
		matchOnDetail: true
	});
	if (!chosen) return;
	const picked = presets.find(p => chosen.description === p.id || chosen.description?.includes(p.id));
	if (!picked) return;

	await vscode.workspace.getConfiguration(CFG).update('presets.active', picked.id, vscode.ConfigurationTarget.Global);
	try {
		await vscode.commands.executeCommand('openCursor.chat.setSystemPrompt', picked.systemPrompt);
	} catch { /* OpenCursor command may not exist in all forks; the setting is the source of truth. */ }
	vscode.window.showInformationMessage(`Arceus preset: ${picked.name}`);
}

async function pickCommand() {
	const commands = allCommands();
	const items: vscode.QuickPickItem[] = commands.map(c => ({
		label: c.name,
		description: c.id,
		detail: c.description
	}));
	const chosen = await vscode.window.showQuickPick(items, {
		placeHolder: 'Pick a framework command — adds its prompt to the next chat turn',
		matchOnDescription: true,
		matchOnDetail: true
	});
	if (!chosen) return;
	const picked = commands.find(c => c.id === chosen.description);
	if (!picked) return;

	const editor = vscode.window.activeTextEditor;
	let selection = '';
	if (editor && !editor.selection.isEmpty) {
		selection = `\n\n--- Selected (${editor.document.fileName}:${editor.selection.start.line + 1}-${editor.selection.end.line + 1}) ---\n` +
			editor.document.getText(editor.selection) +
			`\n--- End selection ---\n`;
	}

	const userAsk = await vscode.window.showInputBox({
		prompt: `/${picked.id} — add a note (optional)`,
		placeHolder: picked.description ?? ''
	});
	if (userAsk === undefined) return;

	const message = `${picked.prompt}\n\nUser note: ${userAsk || '(none)'}${selection}`;

	try {
		await vscode.commands.executeCommand('openCursor.chat.newChat');
		await vscode.commands.executeCommand('openCursor.chat.sendMessage', message);
	} catch {
		await vscode.env.clipboard.writeText(message);
		vscode.window.showInformationMessage(
			`Arceus: couldn't drive the chat panel directly, copied the /${picked.id} prompt to your clipboard.`
		);
	}
}

async function editPresets() {
	await vscode.commands.executeCommand(
		'workbench.action.openSettings',
		'@id:arceusCode.presets.library'
	);
}

async function editCommands() {
	await vscode.commands.executeCommand(
		'workbench.action.openSettings',
		'@id:arceusCode.commands.library'
	);
}

async function openCurrent() {
	const active = vscode.workspace.getConfiguration(CFG).get<string>('presets.active') ?? 'general';
	const preset = allPresets().find(p => p.id === active);
	if (!preset) {
		vscode.window.showWarningMessage(`Arceus: no preset with id "${active}"`);
		return;
	}
	const doc = await vscode.workspace.openTextDocument({
		language: 'markdown',
		content:
			`# Arceus preset: ${preset.name}\n\n` +
			`- id: \`${preset.id}\`\n` +
			`- tools: \`${preset.tools ?? 'all'}\`\n` +
			`- temperature: \`${preset.temperature ?? 0.7}\`\n\n` +
			`## System prompt\n\n${preset.systemPrompt}\n`
	});
	await vscode.window.showTextDocument(doc, { preview: true });
}

export function activate(ctx: vscode.ExtensionContext) {
	ctx.subscriptions.push(
		vscode.commands.registerCommand('arceusCode.presets.pick', pickPreset),
		vscode.commands.registerCommand('arceusCode.presets.edit', editPresets),
		vscode.commands.registerCommand('arceusCode.presets.openCurrent', openCurrent),
		vscode.commands.registerCommand('arceusCode.commands.pick', pickCommand),
		vscode.commands.registerCommand('arceusCode.commands.edit', editCommands)
	);

	const bar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
	bar.command = 'arceusCode.presets.pick';
	ctx.subscriptions.push(bar);

	const refresh = () => {
		const id = vscode.workspace.getConfiguration(CFG).get<string>('presets.active') ?? 'general';
		const p = allPresets().find(x => x.id === id);
		bar.text = `$(sparkle) ${p?.name ?? id}`;
		bar.tooltip = `Arceus preset — click to change (Ctrl+Alt+P)`;
		bar.show();
	};
	refresh();
	ctx.subscriptions.push(
		vscode.workspace.onDidChangeConfiguration(e => {
			if (e.affectsConfiguration(`${CFG}.presets.active`) ||
			    e.affectsConfiguration(`${CFG}.presets.library`)) refresh();
		})
	);
}

export function deactivate() { /* nothing to clean up */ }
