// ─── Pairing ─────────────────────────────────────────────────────────────────

export interface PairingStartRequest {
  target_node_id?: number;
}

export interface PairingStatus {
  paired_nodes_count: number;
  paired_nodes_list: string[];
}

export interface PairingStartResponse {
  status?: string;
  target_node_id?: number;
}

// ─── Messages ─────────────────────────────────────────────────────────────────

export interface SendMessageRequest {
  dst_id: number;
  payload_hex: string;
  coded?: boolean;
}

export interface SendMessageResponse {
  status?: string;
  message?: string;
}

export interface MessageRecord {
  id?: string | number;
  dst_id: number;
  src_id?: number;
  payload_hex: string;
  timestamp?: string;
  sent_at?: string;
  direction?: 'sent' | 'received';
  status?: string;
  rssi?: number;
  coded?: boolean;
}

export type MessageHistoryResponse = MessageRecord[] | { messages: MessageRecord[] };

// ─── Nodes ────────────────────────────────────────────────────────────────────

export interface Node {
  node_id: number;
  id?: number;
  name?: string;
  is_paired?: boolean;
  paired_code?: string;
  network_mode?: boolean;
  network_ttl?: number;
  paired_at?: string;
  last_seen?: string;
  counter?: number;
  rssi?: number;
  coding_enabled?: boolean;
}

export type NodesResponse = Node[] | { nodes: Node[] };

export interface NodeActionResponse {
  status?: string;
  message?: string;
}

// ─── System ──────────────────────────────────────────────────────────────────

export interface RadioInfo {
  frequency?: number;
  bandwidth?: number;
  spreading_factor?: number;
  coding_rate?: string;
  tx_power?: number;
  sync_word?: number | string;
}

export interface SystemInfo {
  gateway_id?: number;
  gateway_id_hex?: string;
  protocol_version?: number;
  status?: string;
  // extended fields (may not be present in all gateway versions)
  id?: number;
  version?: string;
  firmware?: string;
  uptime?: number;
  uptime_str?: string;
  radio?: RadioInfo;
  nodes_count?: number;
  hostname?: string;
  ip?: string;
  platform?: string;
  cpu_temp?: number;
  memory_used?: number;
  memory_total?: number;
}

export interface SystemPairResponse {
  status?: string;
  message?: string;
}

// ─── Logs ────────────────────────────────────────────────────────────────────

export type LogLevel = 'DEBUG' | 'INFO' | 'WARNING' | 'ERROR' | 'CRITICAL';

export interface LogEntry {
  timestamp?: string;
  level?: LogLevel | string;
  message: string;
  source?: string;
  logger?: string;
}

export type LogsResponse = LogEntry[] | string[] | { logs: LogEntry[] | string[] };

// ─── Auth & Users ─────────────────────────────────────────────────────────────

export type UserRole = 'admin' | 'user'

export type Capability =
  | 'dashboard'
  | 'pairing'
  | 'nodes'
  | 'messages'
  | 'logs'
  | 'system'
  | 'users'

export const ALL_CAPABILITIES: Capability[] = [
  'dashboard',
  'pairing',
  'nodes',
  'messages',
  'logs',
  'system',
]

export interface UserInfo {
  id: number
  username: string
  role: UserRole
  permissions: Capability[]
  is_active: boolean
  created_at: string
  updated_at: string
}

export interface AuthToken {
  access_token: string
  token_type: string
  user: UserInfo
}

export interface UserCreate {
  username: string
  password: string
  role: UserRole
  permissions: Capability[]
  is_active: boolean
}

export interface UserUpdate {
  role?: UserRole
  permissions?: Capability[]
  is_active?: boolean
  password?: string
}

// ─── Generic ─────────────────────────────────────────────────────────────────

export interface ApiError {
  detail?: string | ApiValidationError[];
  message?: string;
  status?: number;
}

export interface ApiValidationError {
  loc: (string | number)[];
  msg: string;
  type: string;
}
