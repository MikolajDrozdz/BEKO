import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { ThemeProvider } from './context/ThemeContext'
import { AppShell } from './components/layout/AppShell'
import { DashboardPage } from './features/dashboard/DashboardPage'
import { PairingPage } from './features/pairing/PairingPage'
import { NodesPage } from './features/nodes/NodesPage'
import { MessengerPage } from './features/messages/MessengerPage'
import { SystemPage } from './features/system/SystemPage'
import { LogsPage } from './features/logs/LogsPage'

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 10_000,
      gcTime: 5 * 60_000,
      retry: (failureCount, error) => {
        if (error && typeof error === 'object' && 'status' in error) {
          const status = (error as { status: number }).status
          if (status === 404 || status === 422) return false
        }
        return failureCount < 2
      },
    },
  },
})

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <ThemeProvider>
        <BrowserRouter>
          <Routes>
            <Route element={<AppShell />}>
              <Route path="/" element={<DashboardPage />} />
              <Route path="/pairing" element={<PairingPage />} />
              <Route path="/nodes" element={<NodesPage />} />
              <Route path="/messenger" element={<MessengerPage />} />
              <Route path="/compose" element={<Navigate to="/messenger" replace />} />
              <Route path="/history" element={<Navigate to="/messenger" replace />} />
              <Route path="/system" element={<SystemPage />} />
              <Route path="/logs" element={<LogsPage />} />
              <Route path="*" element={<Navigate to="/" replace />} />
            </Route>
          </Routes>
        </BrowserRouter>
      </ThemeProvider>
    </QueryClientProvider>
  )
}
