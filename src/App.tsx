import { type ReactNode } from 'react'
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { ThemeProvider } from './context/ThemeContext'
import { AuthProvider, useAuth } from './context/AuthContext'
import { AppShell } from './components/layout/AppShell'
import { AccessDenied } from './components/common/AccessDenied'
import { LoginPage } from './features/auth/LoginPage'
import { ProtectedRoute } from './features/auth/ProtectedRoute'
import { DashboardPage } from './features/dashboard/DashboardPage'
import { PairingPage } from './features/pairing/PairingPage'
import { NodesPage } from './features/nodes/NodesPage'
import { MessengerPage } from './features/messages/MessengerPage'
import { SystemPage } from './features/system/SystemPage'
import { LogsPage } from './features/logs/LogsPage'
import { UsersPage } from './features/admin/UsersPage'
import type { Capability } from './types/api'

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 10_000,
      gcTime: 5 * 60_000,
      retry: (failureCount, error) => {
        if (error && typeof error === 'object' && 'status' in error) {
          const status = (error as { status: number }).status
          if (status === 401 || status === 403 || status === 404 || status === 422) return false
        }
        return failureCount < 2
      },
    },
  },
})

function PermissionPage({
  permission,
  children,
}: {
  permission: Capability
  children: ReactNode
}) {
  const { hasPermission } = useAuth()
  return hasPermission(permission) ? <>{children}</> : <AccessDenied />
}

function AdminPage({ children }: { children: ReactNode }) {
  const { isAdmin } = useAuth()
  return isAdmin ? <>{children}</> : <AccessDenied />
}

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <ThemeProvider>
        <AuthProvider>
          <BrowserRouter>
            <Routes>
              <Route path="/login" element={<LoginPage />} />
              <Route element={<ProtectedRoute />}>
                <Route element={<AppShell />}>
                  <Route
                    path="/"
                    element={
                      <PermissionPage permission="dashboard">
                        <DashboardPage />
                      </PermissionPage>
                    }
                  />
                  <Route
                    path="/pairing"
                    element={
                      <PermissionPage permission="pairing">
                        <PairingPage />
                      </PermissionPage>
                    }
                  />
                  <Route
                    path="/nodes"
                    element={
                      <PermissionPage permission="nodes">
                        <NodesPage />
                      </PermissionPage>
                    }
                  />
                  <Route
                    path="/messenger"
                    element={
                      <PermissionPage permission="messages">
                        <MessengerPage />
                      </PermissionPage>
                    }
                  />
                  <Route path="/compose" element={<Navigate to="/messenger" replace />} />
                  <Route path="/history" element={<Navigate to="/messenger" replace />} />
                  <Route
                    path="/system"
                    element={
                      <PermissionPage permission="system">
                        <SystemPage />
                      </PermissionPage>
                    }
                  />
                  <Route
                    path="/logs"
                    element={
                      <PermissionPage permission="logs">
                        <LogsPage />
                      </PermissionPage>
                    }
                  />
                  <Route
                    path="/users"
                    element={
                      <AdminPage>
                        <UsersPage />
                      </AdminPage>
                    }
                  />
                  <Route path="*" element={<Navigate to="/" replace />} />
                </Route>
              </Route>
            </Routes>
          </BrowserRouter>
        </AuthProvider>
      </ThemeProvider>
    </QueryClientProvider>
  )
}
