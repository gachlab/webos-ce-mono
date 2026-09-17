// hp.css arrives as text: esbuild is told to load .css that way, so the same
// stylesheet the page links is also the one every element's shadow root
// adopts (see src/ui/element.ts).
declare module "*.css" {
    const css: string;
    export default css;
}
