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
  ack_required?: boolean;
}

export interface SendMessageResponse {
  status?: string;
  message?: string;
}

export type MessageStatus =
  | 'pending'
  | 'sent'
  | 'failed'
  | 'delivered'
  | 'sent_waiting_response'
  | 'delivered_waiting_response'
  | 'answered'
  | 'received'
  | 'response'
  | 'ok'
  | string;

export interface MessageRecord {
  id?: string | number;
  dst_id: number;
  src_id?: number;
  is_ack?: boolean;
  ack_required?: boolean;
  ack_sent?: boolean;
  message_type?: string;
  payload_hex: string;
  timestamp?: string;
  sent_at?: string;
  direction?: 'sent' | 'received';
  status?: MessageStatus;
  rssi?: number;
  coded?: boolean;
}

export type MessageHistoryResponse = MessageRecord[] | { messages: MessageRecord[] };

export interface MessageStatsBucket {
  time?: string;
  timestamp?: string;
  sent?: number;
  received?: number;
  failed?: number;
  response?: number;
  total?: number;
}

export interface MessageStats {
  total?: number;
  sent?: number;
  received?: number;
  response?: number;
  failed?: number;
  pending?: number;
  answered?: number;
  avg_ack_ms?: number;
  avg_response_ms?: number;
  by_status?: Record<string, number>;
  by_bucket?: MessageStatsBucket[];
}

export interface PendingAck {
  message_id?: string | number;
  dst_id: number;
  sent_at?: string;
  retry_count?: number;
}

export interface PendingResponse {
  message_id?: string | number;
  dst_id: number;
  question?: string;
  expires_at?: string;
}

export interface MessagePending {
  pending_ack?: PendingAck[];
  pending_response?: PendingResponse[];
}

// ─── Nodes ────────────────────────────────────────────────────────────────────

export interface Node {
  node_id: number;
  id?: number;
  name?: string;
  is_paired?: boolean;
  online?: boolean;
  paired_code?: string;
  network_mode?: boolean;
  network_ttl?: number;
  paired_at?: string;
  last_seen?: string;
  counter?: number;
  tx_counter?: number;
  rx_counter?: number;
  rssi?: number;
  snr?: number;
  battery_percent?: number;
  firmware?: string;
  messages_sent?: number;
  messages_delivered?: number;
  messages_failed?: number;
  pending_response?: boolean;
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
  frequency_hz?: number;
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
  online?: boolean;
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
  frequency_hz?: number;
  spreading_factor?: number;
  tx_power?: number;
  radio_ready?: boolean;
  radio_driver?: string;
  radio_error?: string | null;
  gateway?: GatewayInfo;
}

export interface SystemPairResponse {
  status?: string;
  message?: string;
}

export interface GatewayInfo {
  online?: boolean;
  gateway_id?: number;
  gateway_id_hex?: string;
  hostname?: string;
  platform?: string;
  version?: string;
  frequency_hz?: number;
  spreading_factor?: number;
  tx_power?: number;
}

export interface SystemMetric {
  timestamp?: string;
  time?: string;
  uptime?: number;
  cpu_percent?: number;
  cpu_temp?: number;
  load_avg?: number[];
  memory_used?: number;
  memory_total?: number;
  memory_percent?: number;
  disk_used?: number;
  disk_total?: number;
  disk_percent?: number;
}

export type SystemMetricsHistoryResponse = SystemMetric[] | { metrics: SystemMetric[] };

export interface RadioStatus {
  ready?: boolean;
  driver?: string;
  last_error?: string | null;
  runtime_label?: string;
  frequency_hz?: number;
  bandwidth?: number;
  spreading_factor?: number;
  coding_rate?: string;
  tx_power?: number;
  sync_word?: string | number;
  rx_count?: number;
  tx_count?: number;
  tx_fail_count?: number;
  crc_error_count?: number;
  counters?: {
    rx?: number;
    tx?: number;
    tx_failed?: number;
    crc_errors?: number;
    total?: number;
  };
  last_rx_at?: string;
  last_tx_at?: string;
  last_rssi?: number;
  last_snr?: number;
}

// ─── Logs ────────────────────────────────────────────────────────────────────

export type LogLevel = 'DEBUG' | 'INFO' | 'WARNING' | 'ERROR' | 'CRITICAL';

export interface LogEntry {
  id?: string | number;
  timestamp?: string;
  created_at?: string;
  level?: LogLevel | string;
  message: string;
  event?: string;
  source?: string;
  logger?: string;
}

export type LogsResponse = LogEntry[] | string[] | { logs: LogEntry[] | string[] };

export interface LogsSummary {
  by_level?: Record<string, number>;
  counts?: Record<string, number>;
  last_timestamp?: string;
  last_created_at?: string;
  total?: number;
}

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
