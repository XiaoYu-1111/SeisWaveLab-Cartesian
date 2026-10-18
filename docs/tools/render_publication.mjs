// Render mathematics using existing modules; all generated files stay in outer out.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const modules = process.argv[2];
if (!modules) throw new Error('Usage: node docs/tools/render_publication.mjs <existing node_modules>');
const require = createRequire(path.join(path.resolve(modules), '__seiswave_publication__.cjs'));
const MarkdownIt = require('markdown-it');
// Native ESM avoids the plugin's synchronous CJS-to-ESM deasync bridge.
const mathModule = await import(pathToFileURL(path.join(path.dirname(require.resolve('markdown-it-mathjax3')), 'index.js')).href);
const renderer = new MarkdownIt({html:true, breaks:true, linkify:true}).use(mathModule.default || mathModule);
const outer = path.resolve(root, '../out');
const article = path.join(outer,'blog/SeisWaveLab/hexo/source/_posts/CUDA_Seis3D.md');
const assets = article.slice(0,-3);
const markdown = fs.readFileSync(article,'utf8').replace(/^---\r?\n[\s\S]*?\r?\n---\r?\n/,'');
const html = renderer.render(markdown);
const work=path.join(outer,'docs-work/SeisWaveLab');
fs.mkdirSync(work,{recursive:true});
fs.writeFileSync(path.join(work,'rendered-body.html'),html);
const counts = {date:'2026-10-18',
    article_sha256:createHash('sha256').update(fs.readFileSync(article)).digest('hex'),
    renderer:require('markdown-it/package.json').version,
    mathematics:require('markdown-it-mathjax3/package.json').version,
    math_containers:(html.match(/<mjx-container\b/g)||[]).length,
    display_formulas:(html.match(/<mjx-container\b[^>]*display="true"/g)||[]).length,
    expected_numbered_formulas:(markdown.match(/^\\tag\{\d+\}/gm)||[]).length,
    errors:(html.match(/<[^>]*data-mjx-error|<merror\b|<g\b[^>]*data-mml-node="merror"|<script[^>]*math\/tex/g)||[]).length,
    images:[], local_preview_only:true};
for (const match of html.matchAll(/<img\b[^>]*src="([^"]+)"/g)) {
    const target=decodeURIComponent(match[1]);
    if (/^https?:/.test(target)) throw new Error('Main article must use packaged screenshots');
    if (!fs.existsSync(path.resolve(assets,target))) throw new Error(`Missing image ${target}`);
    counts.images.push(target);
}
if(counts.expected_numbered_formulas!==19 || counts.display_formulas!==19 || counts.errors || counts.images.length!==40)
    throw new Error(JSON.stringify(counts));
const page=`<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><base href="${pathToFileURL(assets+path.sep).href}"><title>SeisWave Lab 文档预览</title><style>
body{max-width:1100px;margin:40px auto;padding:0 30px;background:#faf9f5;color:#283c40;font:17px/1.9 "Microsoft YaHei",sans-serif}
h1,h2,h3{line-height:1.45}h2{margin-top:3em;border-bottom:1px solid #cdd8d4}img{max-width:100%;height:auto}table{border-collapse:collapse;width:100%;font-size:15px}td,th{border:1px solid #cbd5d1;padding:8px}th{background:#e8ede8}pre{overflow:auto;background:#eef0eb;padding:16px}mjx-container[display="true"]{overflow-x:auto;overflow-y:hidden;padding:12px 0}a{color:#006d72}
</style></head><body>${html}</body></html>`;
fs.writeFileSync(path.join(work,'preview.html'),page);
for(const location of ['documentation/SeisWaveLab','blog/SeisWaveLab', 'docs-work/SeisWaveLab'])
    fs.writeFileSync(path.join(outer,location,'math-render-validation.json'),JSON.stringify(counts,null,2));
console.log(JSON.stringify({...counts,images:counts.images.length,preview:path.join(work,'preview.html')},null,2));
