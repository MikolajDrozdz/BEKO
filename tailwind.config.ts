import type { Config } from 'tailwindcss'

export default {
  darkMode: 'class',
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        ivory: {
          50: '#f2faeb',
          100: '#e6f5d6',
          200: '#ccebad',
          300: '#b3e085',
          400: '#99d65c',
          500: '#80cc33',
          600: '#66a329',
          700: '#4d7a1f',
          800: '#335214',
          900: '#1a290a',
          950: '#121d07',
        },
        teal: {
          50: '#f0f4f1',
          100: '#e2e9e2',
          200: '#c5d3c6',
          300: '#a8bda9',
          400: '#8ba78d',
          500: '#6e9170',
          600: '#58745a',
          700: '#425743',
          800: '#2c3a2d',
          900: '#161d16',
          950: '#0f1410',
        },
        olive: {
          50: '#f1f3f1',
          100: '#e3e8e3',
          200: '#c7d1c8',
          300: '#acb9ac',
          400: '#90a291',
          500: '#748b75',
          600: '#5d6f5e',
          700: '#465346',
          800: '#2e382f',
          900: '#171c17',
          950: '#101310',
        },
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', 'sans-serif'],
        mono: ['"JetBrains Mono"', '"Fira Code"', 'monospace'],
      },
      animation: {
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
        'fade-in': 'fadeIn 0.2s ease-out',
        'slide-in': 'slideIn 0.2s ease-out',
      },
      keyframes: {
        fadeIn: {
          '0%': { opacity: '0' },
          '100%': { opacity: '1' },
        },
        slideIn: {
          '0%': { opacity: '0', transform: 'translateY(4px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        },
      },
    },
  },
  plugins: [],
} satisfies Config
