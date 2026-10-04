export type NotesSaveStatus = 'saved' | 'unsaved' | 'saving' | 'error';

export interface NotesEditorState {
  notes: string;
  savedNotes: string;
  revision: number;
  status: NotesSaveStatus;
  error: string;
}

interface SavedNotes { notes: string; revision: number }

export class NotesEditor<T extends SavedNotes = SavedNotes> {
  private state: NotesEditorState;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private inFlight: Promise<boolean> | null = null;
  private onState: ((state: NotesEditorState) => void) | null;
  private onSaved: ((result: T) => void) | null;

  constructor(options: {
    notes: string;
    revision: number;
    save: (revision: number, notes: string) => Promise<T>;
    onState: (state: NotesEditorState) => void;
    onSaved?: (result: T) => void;
    delayMs?: number;
  }) {
    this.state = { notes: options.notes, savedNotes: options.notes, revision: options.revision, status: 'saved', error: '' };
    this.save = options.save;
    this.onState = options.onState;
    this.onSaved = options.onSaved || null;
    this.delayMs = options.delayMs ?? 700;
    this.emit();
  }

  private readonly save: (revision: number, notes: string) => Promise<T>;
  private readonly delayMs: number;

  get snapshot() { return { ...this.state }; }
  get hasPendingChanges() { return this.state.notes !== this.state.savedNotes || this.inFlight !== null; }

  private emit() { this.onState?.({ ...this.state }); }
  private clearTimer() { if (this.timer !== null) clearTimeout(this.timer); this.timer = null; }

  change(notes: string, immediate = false) {
    this.clearTimer();
    this.state.notes = notes;
    this.state.error = '';
    if (this.inFlight) {
      this.state.status = 'saving';
    } else if (notes === this.state.savedNotes) {
      this.state.status = 'saved';
    } else {
      this.state.status = 'unsaved';
      if (immediate) void this.flush();
      else this.timer = setTimeout(() => { void this.flush(); }, this.delayMs);
    }
    this.emit();
  }

  async flush(): Promise<boolean> {
    this.clearTimer();
    if (this.inFlight) {
      const completed = await this.inFlight;
      return completed ? this.flush() : false;
    }
    if (this.state.notes === this.state.savedNotes) {
      this.state.status = 'saved'; this.emit(); return true;
    }
    const notes = this.state.notes;
    const revision = this.state.revision;
    this.state.status = 'saving'; this.state.error = ''; this.emit();
    this.inFlight = this.save(revision, notes).then(result => {
      this.state.revision = result.revision;
      this.state.savedNotes = result.notes;
      if (this.state.notes === notes) this.state.notes = result.notes;
      this.state.status = this.state.notes === result.notes ? 'saved' : 'unsaved';
      this.onSaved?.(result);
      this.emit();
      return true;
    }).catch(cause => {
      this.state.status = 'error';
      this.state.error = cause instanceof Error ? cause.message : String(cause);
      this.emit();
      return false;
    }).finally(() => { this.inFlight = null; });
    const completed = await this.inFlight;
    return completed && this.state.notes !== this.state.savedNotes ? this.flush() : completed;
  }

  dispose() {
    this.clearTimer();
    this.onState = null;
    this.onSaved = null;
    if (this.state.notes !== this.state.savedNotes && this.state.status !== 'error') void this.flush();
  }
}

export type MarkdownImportErrorCode = 'type' | 'size' | 'encoding' | 'empty' | 'length';
export class MarkdownImportError extends Error {
  constructor(readonly code: MarkdownImportErrorCode) { super(`Invalid Markdown file: ${code}`); }
}

export async function readMarkdownNotes(file: Pick<File, 'name' | 'size' | 'arrayBuffer'>): Promise<string> {
  if (!file.name.toLowerCase().endsWith('.md')) throw new MarkdownImportError('type');
  if (file.size > 64 * 1024) throw new MarkdownImportError('size');
  let notes: string;
  try { notes = new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer()); }
  catch { throw new MarkdownImportError('encoding'); }
  if (!notes.trim()) throw new MarkdownImportError('empty');
  if (notes.length > 10000) throw new MarkdownImportError('length');
  return notes;
}
