// A stylesheet arrives as text: esbuild is told to load .css that way, so
// kit.css is what every element adopts into its shadow root (see element.ts)
// while page.css is linked by the page.
//
// It lives here and not in @webos/api because only this package imports a
// stylesheet -- and with it there, @webos/ui-kit could not be type-checked
// without the platform package tagging along for a reason nothing declared.
declare module "*.css" {
    const css: string;
    export default css;
}
