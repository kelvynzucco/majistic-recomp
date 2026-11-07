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
        majin: {
          DEFAULT: "#6da712",
          hover: "#7cb818",
          dark: "#55820e",
          light: "#a3cc54",
        },
        emerald: {
          50: '#f6faec',
          100: '#ebf5d6',
          200: '#d7ebb0',
          300: '#bedd80',
          400: '#a3cc54',
          500: '#7cb818',
          600: '#6da712',
          700: '#53820e',
          800: '#436611',
          900: '#385513',
          950: '#1c2e06',
        },
      },
    },
  },
  plugins: [],
}
