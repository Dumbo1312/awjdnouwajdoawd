const esbuild = require('esbuild');
const watch = process.argv.includes('--watch');

const opts = {
	entryPoints: ['src/extension.ts'],
	bundle: true,
	outfile: 'dist/extension.js',
	platform: 'node',
	target: 'node18',
	external: ['vscode'],
	format: 'cjs',
	sourcemap: true,
	minify: !watch,
	logLevel: 'info'
};

(async () => {
	if (watch) {
		const ctx = await esbuild.context(opts);
		await ctx.watch();
	} else {
		await esbuild.build(opts);
	}
})();
