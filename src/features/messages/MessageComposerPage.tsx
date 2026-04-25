import { useState, useEffect } from 'react'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { Send, Radio, AlertTriangle, Lock, CheckCheck } from 'lucide-react'
import { useSendMessage } from '@/hooks/useMessages'
import { useNodes } from '@/hooks/useNodes'
import { SectionHeader } from '@/components/common/SectionHeader'
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Textarea } from '@/components/ui/textarea'
import { Label } from '@/components/ui/label'
import { Switch } from '@/components/ui/switch'
import { Badge } from '@/components/ui/badge'
import { Alert, AlertDescription } from '@/components/ui/alert'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { textToHex, isValidHex, formatHex, hexToText } from '@/lib/utils/hex'
import {
  BROADCAST_ID,
  MAX_PAYLOAD_BYTES,
  getHexByteLength,
  getSendTargetError,
  getUtf8ByteLength,
  hasOnlyAsciiBytes,
  isAsciiText,
  isNodeAddress,
  requiresNodeResponse,
} from '@/lib/utils/protocol'
import { formatNodeId } from '@/lib/utils/format'
import type { Node } from '@/types/api'

const schema = z.object({
  recipient: z.string().min(1, 'Select a recipient'),
  message: z.string().optional(),
  advancedMode: z.boolean().default(false),
  rawHex: z.string().optional(),
  coded: z.boolean().default(false),
  ackRequired: z.boolean().default(true),
})

type FormValues = z.infer<typeof schema>

export function MessageComposerPage() {
  const { data: nodes } = useNodes()
  const sendMessage = useSendMessage()
  const [hexPreview, setHexPreview] = useState('')
  const [hexError, setHexError] = useState('')
  const [, setShowAdvanced] = useState(false)

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: {
      recipient: '',
      message: '',
      advancedMode: false,
      rawHex: '',
      coded: false,
      ackRequired: true,
    },
  })

  const watchMessage = form.watch('message')
  const watchRawHex = form.watch('rawHex')
  const watchAdvanced = form.watch('advancedMode')
  const watchCoded = form.watch('coded')
  const watchAckRequired = form.watch('ackRequired')
  const watchRecipient = form.watch('recipient')
  const recipientId = Number(watchRecipient)
  const destinationError = watchRecipient ? getSendTargetError(recipientId) : undefined
  const cleanRawHex = formatHex(watchRawHex ?? '')
  const rawHexValid = !watchRawHex || isValidHex(watchRawHex)
  const payloadByteLength = watchAdvanced
    ? (rawHexValid ? getHexByteLength(watchRawHex ?? '') : 0)
    : getUtf8ByteLength(watchMessage ?? '')
  const payloadLengthError = payloadByteLength > MAX_PAYLOAD_BYTES
    ? `Payload ma ${payloadByteLength} B, limit ramki to ${MAX_PAYLOAD_BYTES} B.`
    : undefined
  const asciiError = watchAdvanced
    ? (rawHexValid && !hasOnlyAsciiBytes(cleanRawHex) ? 'Payload może zawierać tylko bajty ASCII.' : undefined)
    : (!isAsciiText(watchMessage ?? '') ? 'Wiadomość może zawierać tylko znaki ASCII.' : undefined)
  const responseSource = watchAdvanced && rawHexValid ? hexToText(cleanRawHex) : (watchMessage ?? '')
  const responseRequired = requiresNodeResponse(responseSource)
  const isBroadcast = watchRecipient === String(BROADCAST_ID)
  const effectiveAckRequired = !isBroadcast && watchAckRequired

  // Auto-sync text → hex preview
  useEffect(() => {
    if (!watchAdvanced && watchMessage) {
      const hex = textToHex(watchMessage)
      setHexPreview(hex)
      form.setValue('rawHex', hex)
      setHexError('')
    }
  }, [watchMessage, watchAdvanced])

  // Validate raw hex in advanced mode
  useEffect(() => {
    if (watchAdvanced && watchRawHex) {
      if (!isValidHex(watchRawHex)) {
        setHexError('Invalid hex string. Must be even-length hex characters only.')
      } else {
        setHexError('')
        setHexPreview(formatHex(watchRawHex))
      }
    }
  }, [watchRawHex, watchAdvanced])

  function onSubmit(values: FormValues) {
    const dstId = Number(values.recipient)
    const targetError = getSendTargetError(dstId)
    if (targetError) {
      form.setError('recipient', { message: targetError })
      return
    }

    const payload = watchAdvanced
      ? formatHex(values.rawHex ?? '')
      : textToHex(values.message ?? '')

    if (!payload) {
      form.setError('message', { message: 'Message cannot be empty' })
      return
    }

    if (watchAdvanced && !isValidHex(payload)) {
      return
    }

    if (payloadLengthError) {
      form.setError(watchAdvanced ? 'rawHex' : 'message', { message: payloadLengthError })
      return
    }

    if (asciiError) {
      form.setError(watchAdvanced ? 'rawHex' : 'message', { message: asciiError })
      return
    }

    sendMessage.mutate({
      dst_id: dstId,
      payload_hex: payload,
      coded: values.coded,
      ack_required: effectiveAckRequired,
    })
  }

  const selectedNode: Node | undefined = nodes?.find(
    (n) => String(n.node_id ?? n.id) === watchRecipient,
  )

  return (
    <div className="space-y-6 max-w-2xl">
      <SectionHeader
        title="Compose Message"
        description="Send a message to a node or broadcast to all"
      />

      <form onSubmit={form.handleSubmit(onSubmit)} className="space-y-4">
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Radio className="h-4 w-4 text-ivory-700 dark:text-ivory-400" />
              Recipient
            </CardTitle>
            <CardDescription>Select a target node or broadcast</CardDescription>
          </CardHeader>
          <CardContent className="space-y-3">
            <div className="space-y-1.5">
              <Label>Destination</Label>
              <Select
                value={form.watch('recipient')}
                onValueChange={(v) => form.setValue('recipient', v)}
              >
                <SelectTrigger className={form.formState.errors.recipient ? 'border-red-500' : ''}>
                  <SelectValue placeholder="Select recipient..." />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value={String(BROADCAST_ID)}>
                    <div className="flex items-center gap-2">
                      <Radio className="h-3.5 w-3.5" />
                      <span>Broadcast</span>
                    </div>
                  </SelectItem>
                  {nodes?.filter((node) => isNodeAddress(node.node_id ?? node.id ?? 0)).map((node) => {
                    const id = node.node_id ?? node.id ?? 0
                    return (
                      <SelectItem key={id} value={String(id)}>
                        <div className="flex items-center gap-2">
                          <span className="font-mono-feature">{formatNodeId(id)}</span>
                          {node.name && <span className="text-teal-500">— {node.name}</span>}
                          {node.coding_enabled && (
                            <Badge variant="accent" className="text-[10px] py-0">coded</Badge>
                          )}
                        </div>
                      </SelectItem>
                    )
                  })}
                </SelectContent>
              </Select>
              {form.formState.errors.recipient && (
                <p className="text-xs text-red-500">{form.formState.errors.recipient.message}</p>
              )}
              {destinationError && (
                <p className="text-xs text-red-500">{destinationError}</p>
              )}
            </div>

            {isBroadcast && (
              <Alert variant="warning">
                <AlertTriangle className="h-4 w-4" />
                <AlertDescription>
                  Message will be sent to all paired nodes (Broadcast)
                </AlertDescription>
              </Alert>
            )}

            {selectedNode && (
              <div className="flex flex-wrap items-center gap-2 text-xs text-teal-600 dark:text-teal-400">
                <span>Node ID:</span>
                <Badge variant="outline" className="font-mono-feature">
                  {selectedNode.node_id !== undefined ? `0x${selectedNode.node_id.toString(16).toUpperCase().padStart(4, '0')}` : '—'}
                </Badge>
                {selectedNode.network_mode && (
                  <Badge variant="accent">network mode</Badge>
                )}
              </div>
            )}
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <div className="flex items-center justify-between">
              <div>
                <CardTitle>Message</CardTitle>
                <CardDescription>
                  {watchAdvanced ? 'Enter raw hex payload' : 'Type your message — auto-converted to hex'}
                </CardDescription>
              </div>
              <div className="flex items-center gap-2">
                <Label htmlFor="advanced-mode" className="text-xs">Advanced (raw hex)</Label>
                <Switch
                  id="advanced-mode"
                  checked={watchAdvanced}
                  onCheckedChange={(v) => {
                    form.setValue('advancedMode', v)
                    setShowAdvanced(v)
                    setHexError('')
                  }}
                />
              </div>
            </div>
          </CardHeader>
          <CardContent className="space-y-3">
            {!watchAdvanced ? (
              <div className="space-y-1.5">
                <Label htmlFor="message">Plain Text</Label>
                <Textarea
                  id="message"
                  placeholder="Type your message here..."
                  rows={3}
                  error={!!form.formState.errors.message}
                  {...form.register('message')}
                />
                {form.formState.errors.message && (
                  <p className="text-xs text-red-500">{form.formState.errors.message.message}</p>
                )}
              </div>
            ) : (
              <div className="space-y-1.5">
                <Label htmlFor="raw-hex">Raw Hex Payload</Label>
                <Input
                  id="raw-hex"
                  placeholder="e.g. 48656c6c6f"
                  className="font-mono-feature"
                  error={!!hexError}
                  {...form.register('rawHex')}
                />
                {hexError && <p className="text-xs text-red-500">{hexError}</p>}
              </div>
            )}

            {/* Hex Preview */}
            {(hexPreview || (watchAdvanced && watchRawHex)) && !hexError && (
              <div className="rounded-md border border-teal-100 dark:border-teal-800 bg-teal-50 dark:bg-teal-800/30 p-3">
                <p className="text-[10px] uppercase tracking-wide text-teal-500 dark:text-teal-400 mb-1.5 font-medium">
                  payload_hex preview
                </p>
                <p className="font-mono-feature text-xs text-teal-900 dark:text-teal-100 break-all">
                  {hexPreview || formatHex(watchRawHex ?? '')}
                </p>
              </div>
            )}

            <div className="flex flex-wrap items-center justify-between gap-2 text-xs">
              <div className="flex flex-wrap items-center gap-2">
                {responseRequired && (
                  <Badge variant="warning">Wymaga odpowiedzi YES / OK / NO</Badge>
                )}
                {payloadLengthError && (
                  <span className="text-red-500 dark:text-red-400">{payloadLengthError}</span>
                )}
                {asciiError && (
                  <span className="text-red-500 dark:text-red-400">{asciiError}</span>
                )}
              </div>
              <span className={payloadLengthError ? 'text-red-500 dark:text-red-400' : 'text-teal-500 dark:text-teal-400'}>
                {payloadByteLength}/{MAX_PAYLOAD_BYTES} B
              </span>
            </div>

            {responseRequired && (
              <Alert variant="warning">
                <AlertTriangle className="h-4 w-4" />
                <AlertDescription>
                  Backend oznaczy tę wiadomość jako wymagającą odpowiedzi YES / OK / NO.
                </AlertDescription>
              </Alert>
            )}

          </CardContent>
        </Card>

        <div className="flex items-center justify-between gap-3">
          <div className="text-xs text-teal-500 dark:text-teal-400">
            {watchRecipient && (
              <>To: <span className="font-mono-feature font-medium text-teal-700 dark:text-teal-300">
                {isBroadcast ? 'Broadcast' : formatNodeId(Number(watchRecipient))}
              </span></>
            )}
          </div>
          <div className="flex items-center gap-4">
            <div className="flex items-center gap-2">
              <CheckCheck className="h-3.5 w-3.5 text-teal-400" />
              <Label htmlFor="ack-toggle" className="text-xs text-teal-600 dark:text-teal-400 cursor-pointer">
                ACK
              </Label>
              <Switch
                id="ack-toggle"
                checked={effectiveAckRequired}
                onCheckedChange={(v) => form.setValue('ackRequired', v)}
                disabled={isBroadcast}
              />
            </div>
            <div className="flex items-center gap-2">
              <Lock className="h-3.5 w-3.5 text-teal-400" />
              <Label htmlFor="coded-toggle" className="text-xs text-teal-600 dark:text-teal-400 cursor-pointer">
                Coded
              </Label>
              <Switch
                id="coded-toggle"
                checked={watchCoded}
                onCheckedChange={(v) => form.setValue('coded', v)}
              />
            </div>
            <Button
              type="submit"
              loading={sendMessage.isPending}
              disabled={!!hexError || !watchRecipient || !!destinationError || !!payloadLengthError || !!asciiError}
            >
              <Send className="h-4 w-4" />
              Send Message
            </Button>
          </div>
        </div>
      </form>
    </div>
  )
}
