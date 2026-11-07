/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx,html}",
  ],
  theme: {
    extend: {
      colors: {
        darkBg: "#0f1117",
        darkCard: "#181b24",
        darkControl: "#232734",
        darkBorder: "#32384a",
        accent: "#38bdf8",
      },
    },
  },
  plugins: [],
}
