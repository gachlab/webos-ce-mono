// A stylesheet arrives as text: esbuild is told to load .css that way, so
// kit.css is what every element adopts into its shadow root (see
// src/ui/element.ts) while page.css is linked by the page.
declare module "*.css" {
    const css: string;
    export default css;
}
