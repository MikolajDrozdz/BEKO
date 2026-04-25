import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { toast } from 'sonner'
import { useForm, Controller } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import {
  Users,
  Plus,
  Pencil,
  Trash2,
  ShieldCheck,
  UserCog,
  Eye,
  EyeOff,
  RefreshCw,
} from 'lucide-react'
import { usersApi } from '@/lib/api/users'
import { useAuth } from '@/context/AuthContext'
import { SectionHeader } from '@/components/common/SectionHeader'
import { ConfirmDialog } from '@/components/common/ConfirmDialog'
import { EmptyState } from '@/components/common/EmptyState'
import { ErrorDisplay } from '@/components/common/ErrorDisplay'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { Switch } from '@/components/ui/switch'
import { Skeleton } from '@/components/ui/skeleton'
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@/components/ui/dialog'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from '@/components/ui/table'
import { ALL_CAPABILITIES, type UserInfo, type Capability, type UserRole } from '@/types/api'
import { formatTimestamp } from '@/lib/utils/format'

const CAPABILITY_LABELS: Record<Capability, string> = {
  dashboard: 'Dashboard',
  pairing: 'Pairing',
  nodes: 'Nodes',
  messages: 'Messenger',
  logs: 'Logs',
  system: 'System Panel',
  users: 'Users',
}

const USERS_KEY = ['users']

// ─── Form schemas ─────────────────────────────────────────────────────────────

const createSchema = z.object({
  username: z
    .string()
    .min(3, 'At least 3 characters')
    .max(50)
    .regex(/^[a-zA-Z0-9_.-]+$/, 'Letters, digits, _ . - only'),
  password: z.string().min(6, 'At least 6 characters'),
  role: z.enum(['admin', 'user']),
  permissions: z.array(z.string()),
  is_active: z.boolean(),
})
type CreateForm = z.infer<typeof createSchema>

const editSchema = z.object({
  role: z.enum(['admin', 'user']),
  permissions: z.array(z.string()),
  is_active: z.boolean(),
  password: z.string().min(6, 'At least 6 characters').or(z.literal('')),
})
type EditForm = z.infer<typeof editSchema>

// ─── Helpers ──────────────────────────────────────────────────────────────────

function RoleBadge({ role }: { role: UserRole }) {
  if (role === 'admin') {
    return (
      <Badge className="gap-1 bg-ivory-100 dark:bg-ivory-900/50 text-ivory-800 dark:text-ivory-400 border border-ivory-200 dark:border-ivory-800/50">
        <ShieldCheck className="h-2.5 w-2.5" />
        Admin
      </Badge>
    )
  }
  return (
    <Badge variant="outline" className="gap-1">
      <UserCog className="h-2.5 w-2.5" />
      User
    </Badge>
  )
}

function PermissionsCell({ user }: { user: UserInfo }) {
  if (user.role === 'admin') {
    return <span className="text-[10px] text-teal-500 dark:text-teal-400">All (admin)</span>
  }
  const caps = user.permissions.filter((p) => p !== 'users')
  if (caps.length === 0) {
    return <span className="text-[10px] text-teal-400 dark:text-teal-600">None</span>
  }
  return (
    <div className="flex flex-wrap gap-0.5">
      {caps.map((cap) => (
        <Badge key={cap} variant="outline" className="text-[9px] py-0 h-4">
          {CAPABILITY_LABELS[cap as Capability] ?? cap}
        </Badge>
      ))}
    </div>
  )
}

// ─── Permission checkboxes ────────────────────────────────────────────────────

function PermissionCheckboxes({
  value,
  onChange,
  disabled,
}: {
  value: string[]
  onChange: (v: string[]) => void
  disabled?: boolean
}) {
  function toggle(cap: Capability) {
    if (value.includes(cap)) {
      onChange(value.filter((p) => p !== cap))
    } else {
      onChange([...value, cap])
    }
  }
  return (
    <div className="grid grid-cols-2 gap-2">
      {ALL_CAPABILITIES.map((cap) => (
        <label
          key={cap}
          className={`flex items-center gap-2 rounded-md border px-2.5 py-2 cursor-pointer select-none transition-colors ${
            disabled
              ? 'opacity-40 cursor-not-allowed border-teal-100 dark:border-teal-800'
              : value.includes(cap)
              ? 'border-ivory-300 dark:border-ivory-700 bg-ivory-50 dark:bg-ivory-950/30'
              : 'border-teal-100 dark:border-teal-800 hover:border-teal-300 dark:hover:border-teal-700'
          }`}
        >
          <input
            type="checkbox"
            className="accent-ivory-700"
            checked={value.includes(cap)}
            onChange={() => !disabled && toggle(cap)}
            disabled={disabled}
          />
          <span className="text-xs text-teal-900 dark:text-teal-100">{CAPABILITY_LABELS[cap]}</span>
        </label>
      ))}
    </div>
  )
}

// ─── Create dialog ────────────────────────────────────────────────────────────

function CreateUserDialog({ onClose }: { onClose: () => void }) {
  const queryClient = useQueryClient()
  const [showPwd, setShowPwd] = useState(false)

  const form = useForm<CreateForm>({
    resolver: zodResolver(createSchema),
    defaultValues: {
      username: '',
      password: '',
      role: 'user',
      permissions: [],
      is_active: true,
    },
  })

  const role = form.watch('role')

  const mutation = useMutation({
    mutationFn: (data: CreateForm) =>
      usersApi.create({
        ...data,
        permissions: data.permissions as Capability[],
      }),
    onSuccess: () => {
      toast.success('User created')
      queryClient.invalidateQueries({ queryKey: USERS_KEY })
      onClose()
    },
    onError: (err: Error) => toast.error(`Failed: ${err.message}`),
  })

  return (
    <DialogContent className="max-w-md">
      <DialogHeader>
        <DialogTitle>Create User</DialogTitle>
      </DialogHeader>
      <form onSubmit={form.handleSubmit((v) => mutation.mutate(v))} className="space-y-4">
        <div className="space-y-1.5">
          <Label htmlFor="new-username">Username</Label>
          <Input id="new-username" placeholder="operator1" {...form.register('username')} />
          {form.formState.errors.username && (
            <p className="text-xs text-red-500">{form.formState.errors.username.message}</p>
          )}
        </div>

        <div className="space-y-1.5">
          <Label htmlFor="new-password">Password</Label>
          <div className="relative">
            <Input
              id="new-password"
              type={showPwd ? 'text' : 'password'}
              placeholder="••••••••"
              className="pr-9"
              {...form.register('password')}
            />
            <button
              type="button"
              onClick={() => setShowPwd((v) => !v)}
              className="absolute right-2.5 top-1/2 -translate-y-1/2 text-teal-400 hover:text-teal-600 transition-colors"
              tabIndex={-1}
            >
              {showPwd ? <EyeOff className="h-3.5 w-3.5" /> : <Eye className="h-3.5 w-3.5" />}
            </button>
          </div>
          {form.formState.errors.password && (
            <p className="text-xs text-red-500">{form.formState.errors.password.message}</p>
          )}
        </div>

        <div className="space-y-1.5">
          <Label>Role</Label>
          <Controller
            control={form.control}
            name="role"
            render={({ field }) => (
              <Select value={field.value} onValueChange={field.onChange}>
                <SelectTrigger className="h-9">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="user">User</SelectItem>
                  <SelectItem value="admin">Admin</SelectItem>
                </SelectContent>
              </Select>
            )}
          />
        </div>

        {role === 'user' && (
          <div className="space-y-1.5">
            <Label>Capabilities</Label>
            <Controller
              control={form.control}
              name="permissions"
              render={({ field }) => (
                <PermissionCheckboxes value={field.value} onChange={field.onChange} />
              )}
            />
          </div>
        )}
        {role === 'admin' && (
          <p className="text-xs text-teal-500 dark:text-teal-400">
            Admins have access to all capabilities.
          </p>
        )}

        <div className="flex items-center gap-2">
          <Controller
            control={form.control}
            name="is_active"
            render={({ field }) => (
              <Switch id="new-active" checked={field.value} onCheckedChange={field.onChange} />
            )}
          />
          <Label htmlFor="new-active" className="cursor-pointer">Active</Label>
        </div>

        <DialogFooter>
          <Button variant="outline" size="sm" type="button" onClick={onClose}>Cancel</Button>
          <Button size="sm" type="submit" loading={mutation.isPending}>Create</Button>
        </DialogFooter>
      </form>
    </DialogContent>
  )
}

// ─── Edit dialog ──────────────────────────────────────────────────────────────

function EditUserDialog({ user, onClose }: { user: UserInfo; onClose: () => void }) {
  const { user: currentUser } = useAuth()
  const queryClient = useQueryClient()
  const [showPwd, setShowPwd] = useState(false)
  const isSelf = currentUser?.id === user.id

  const form = useForm<EditForm>({
    resolver: zodResolver(editSchema),
    defaultValues: {
      role: user.role,
      permissions: user.role === 'admin' ? [] : user.permissions.filter((p) => p !== 'users'),
      is_active: user.is_active,
      password: '',
    },
  })

  const role = form.watch('role')

  const mutation = useMutation({
    mutationFn: (data: EditForm) =>
      usersApi.update(user.id, {
        role: data.role,
        permissions: data.permissions as Capability[],
        is_active: data.is_active,
        ...(data.password ? { password: data.password } : {}),
      }),
    onSuccess: () => {
      toast.success('User updated')
      queryClient.invalidateQueries({ queryKey: USERS_KEY })
      onClose()
    },
    onError: (err: Error) => toast.error(`Failed: ${err.message}`),
  })

  return (
    <DialogContent className="max-w-md">
      <DialogHeader>
        <DialogTitle>Edit User — {user.username}</DialogTitle>
      </DialogHeader>
      <form onSubmit={form.handleSubmit((v) => mutation.mutate(v))} className="space-y-4">
        <div className="space-y-1.5">
          <Label>Role</Label>
          <Controller
            control={form.control}
            name="role"
            render={({ field }) => (
              <Select
                value={field.value}
                onValueChange={field.onChange}
                disabled={isSelf}
              >
                <SelectTrigger className="h-9">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="user">User</SelectItem>
                  <SelectItem value="admin">Admin</SelectItem>
                </SelectContent>
              </Select>
            )}
          />
          {isSelf && (
            <p className="text-[10px] text-teal-500 dark:text-teal-400">
              You cannot change your own role.
            </p>
          )}
        </div>

        {role === 'user' && (
          <div className="space-y-1.5">
            <Label>Capabilities</Label>
            <Controller
              control={form.control}
              name="permissions"
              render={({ field }) => (
                <PermissionCheckboxes value={field.value} onChange={field.onChange} />
              )}
            />
          </div>
        )}
        {role === 'admin' && (
          <p className="text-xs text-teal-500 dark:text-teal-400">
            Admins have access to all capabilities.
          </p>
        )}

        <div className="flex items-center gap-2">
          <Controller
            control={form.control}
            name="is_active"
            render={({ field }) => (
              <Switch
                id="edit-active"
                checked={field.value}
                onCheckedChange={field.onChange}
                disabled={isSelf}
              />
            )}
          />
          <Label htmlFor="edit-active" className="cursor-pointer">Active</Label>
        </div>

        <div className="space-y-1.5">
          <Label htmlFor="edit-password">New Password <span className="text-teal-400 text-[10px]">(leave blank to keep current)</span></Label>
          <div className="relative">
            <Input
              id="edit-password"
              type={showPwd ? 'text' : 'password'}
              placeholder="••••••••"
              className="pr-9"
              {...form.register('password')}
            />
            <button
              type="button"
              onClick={() => setShowPwd((v) => !v)}
              className="absolute right-2.5 top-1/2 -translate-y-1/2 text-teal-400 hover:text-teal-600 transition-colors"
              tabIndex={-1}
            >
              {showPwd ? <EyeOff className="h-3.5 w-3.5" /> : <Eye className="h-3.5 w-3.5" />}
            </button>
          </div>
          {form.formState.errors.password && (
            <p className="text-xs text-red-500">{form.formState.errors.password.message}</p>
          )}
        </div>

        <DialogFooter>
          <Button variant="outline" size="sm" type="button" onClick={onClose}>Cancel</Button>
          <Button size="sm" type="submit" loading={mutation.isPending}>Save</Button>
        </DialogFooter>
      </form>
    </DialogContent>
  )
}

// ─── Main page ────────────────────────────────────────────────────────────────

export function UsersPage() {
  const { user: currentUser } = useAuth()
  const queryClient = useQueryClient()
  const [createOpen, setCreateOpen] = useState(false)
  const [editTarget, setEditTarget] = useState<UserInfo | null>(null)

  const { data: users, isLoading, error, refetch } = useQuery<UserInfo[]>({
    queryKey: USERS_KEY,
    queryFn: () => usersApi.list(),
  })

  const deleteMutation = useMutation({
    mutationFn: (id: number) => usersApi.delete(id),
    onSuccess: () => {
      toast.success('User deleted')
      queryClient.invalidateQueries({ queryKey: USERS_KEY })
    },
    onError: (err: Error) => toast.error(`Failed: ${err.message}`),
  })

  return (
    <div className="space-y-6">
      <SectionHeader
        title="Users"
        description="Manage operator accounts and access rights"
        actions={
          <div className="flex items-center gap-2">
            <Button variant="outline" size="sm" onClick={() => refetch()}>
              <RefreshCw className="h-3.5 w-3.5" />
              Refresh
            </Button>
            <Button size="sm" onClick={() => setCreateOpen(true)}>
              <Plus className="h-3.5 w-3.5" />
              New User
            </Button>
          </div>
        }
      />

      <Card>
        <CardHeader className="pb-3">
          <CardTitle className="flex items-center gap-2">
            <Users className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
            Operator Accounts
            {users && (
              <Badge variant="outline" className="ml-1">{users.length}</Badge>
            )}
          </CardTitle>
        </CardHeader>
        <CardContent className="p-0">
          {isLoading && (
            <div className="p-5 space-y-2">
              {[...Array(3)].map((_, i) => <Skeleton key={i} className="h-12 w-full" />)}
            </div>
          )}

          {error && (
            <div className="p-5">
              <ErrorDisplay error={error} onRetry={refetch} />
            </div>
          )}

          {!isLoading && !error && (
            <>
              {users && users.length > 0 ? (
                <Table>
                  <TableHeader>
                    <TableRow>
                      <TableHead>Username</TableHead>
                      <TableHead>Role</TableHead>
                      <TableHead>Capabilities</TableHead>
                      <TableHead>Status</TableHead>
                      <TableHead>Created</TableHead>
                      <TableHead className="w-[80px]">Actions</TableHead>
                    </TableRow>
                  </TableHeader>
                  <TableBody>
                    {users.map((u) => (
                      <TableRow key={u.id}>
                        <TableCell>
                          <div className="flex items-center gap-2">
                            <span className="text-sm font-medium text-teal-900 dark:text-teal-50">
                              {u.username}
                            </span>
                            {u.id === currentUser?.id && (
                              <Badge variant="outline" className="text-[9px] py-0 h-4">you</Badge>
                            )}
                          </div>
                        </TableCell>
                        <TableCell><RoleBadge role={u.role} /></TableCell>
                        <TableCell><PermissionsCell user={u} /></TableCell>
                        <TableCell>
                          <Badge
                            variant={u.is_active ? 'success' : 'default'}
                            className="text-[10px]"
                          >
                            {u.is_active ? 'Active' : 'Inactive'}
                          </Badge>
                        </TableCell>
                        <TableCell>
                          <span className="text-xs text-teal-500 dark:text-teal-400">
                            {formatTimestamp(u.created_at)}
                          </span>
                        </TableCell>
                        <TableCell>
                          <div className="flex items-center gap-1">
                            <Button
                              variant="ghost"
                              size="icon-sm"
                              onClick={() => setEditTarget(u)}
                            >
                              <Pencil className="h-3.5 w-3.5" />
                            </Button>
                            <ConfirmDialog
                              trigger={
                                <Button
                                  variant="ghost"
                                  size="icon-sm"
                                  className="text-red-500 hover:text-red-600 dark:text-red-400"
                                  disabled={u.id === currentUser?.id}
                                >
                                  <Trash2 className="h-3.5 w-3.5" />
                                </Button>
                              }
                              title="Delete User"
                              description={`Delete user "${u.username}"? This cannot be undone.`}
                              confirmLabel="Delete"
                              variant="destructive"
                              onConfirm={() => deleteMutation.mutate(u.id)}
                              loading={deleteMutation.isPending && deleteMutation.variables === u.id}
                            />
                          </div>
                        </TableCell>
                      </TableRow>
                    ))}
                  </TableBody>
                </Table>
              ) : (
                <EmptyState
                  icon={Users}
                  title="No users yet"
                  description="Create the first operator account."
                  className="py-16"
                />
              )}
            </>
          )}
        </CardContent>
      </Card>

      {/* Create dialog */}
      <Dialog open={createOpen} onOpenChange={setCreateOpen}>
        {createOpen && <CreateUserDialog onClose={() => setCreateOpen(false)} />}
      </Dialog>

      {/* Edit dialog */}
      <Dialog open={!!editTarget} onOpenChange={(open) => !open && setEditTarget(null)}>
        {editTarget && <EditUserDialog user={editTarget} onClose={() => setEditTarget(null)} />}
      </Dialog>
    </div>
  )
}
