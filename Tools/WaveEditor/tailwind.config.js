/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        dark: {
          900: '#0b0f19',
          800: '#111827',
          700: '#1f2937',
          600: '#374151'
        },
        frost: {
          cyan: '#38bdf8',
          blue: '#2563eb',
          ice: '#e0f2fe'
        }
      }
    },
  },
  plugins: [],
}