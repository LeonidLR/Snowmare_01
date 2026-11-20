// Training Ground: the full-window frame shared by «Управление картой» (T-022) and «Пропсы уровней» (T-023). `useFullWindow` owns the
// flag and the F11 / Esc handling (utils/fullWindow.ts); `FullWindowFrame` wraps the plan: normally a transparent div, in full mode a
// fixed overlay over the whole browser window (header bar, the plan, an optional collapsible side drawer for the layers / settings).
// The frame keeps its children at the SAME place of the React tree in both modes (only the classes change), so the plan canvas is not
// re-created: the zoom / pan and the selection survive the toggle, the plan only re-measures itself ("Вписать" fits the new size).
import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Minimize2, PanelRightClose, PanelRightOpen } from 'lucide-react';
import { fullWindowAction } from '../utils/fullWindow';

/**
 * The full-window flag. `enabled`: the page is the active one (F11 is taken only then). `escExits`: false while the page needs Esc
 * itself. Also locks the page scroll while the mode is on and leaves it when the page goes away.
 */
export function useFullWindow(enabled: boolean, escExits = true): { full: boolean; toggle: () => void; setFull: (v: boolean) => void } {
  const [full, setFull] = useState(false);
  const stateRef = useRef({ enabled, full, escExits });
  stateRef.current = { enabled, full, escExits };
  const toggle = useCallback(() => setFull((v) => !v), []);
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const a = fullWindowAction(e, stateRef.current);
      if (!a) return;
      e.preventDefault();
      setFull(a === 'toggle' ? !stateRef.current.full : false);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);
  useEffect(() => { if (!enabled) setFull(false); }, [enabled]);
  useEffect(() => {
    if (!full) return;
    const prev = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => { document.body.style.overflow = prev; };
  }, [full]);
  return { full, toggle, setFull };
}

interface FrameProps {
  full: boolean;
  onExit: () => void;
  /** The bar shown only in full mode (title, undo / redo, accept ...). */
  header?: React.ReactNode;
  /** The side drawer shown only in full mode (the layers panel / the settings); the frame adds its collapse button. */
  drawer?: React.ReactNode;
  drawerTitle?: string;
  children: React.ReactNode;
}

export const FullWindowFrame: React.FC<FrameProps> = ({ full, onExit, header, drawer, drawerTitle = 'Панель', children }) => {
  const [drawerOpen, setDrawerOpen] = useState(true);
  useEffect(() => { /* the drawer state is kept across toggles */ }, []);
  return (
    <div className={full ? 'fixed inset-0 z-50 flex bg-slate-950' : ''} data-full-window={full ? 'on' : 'off'}>
      <div className={full ? 'flex-1 min-w-0 flex flex-col p-2 gap-1.5 overflow-hidden' : ''}>
        {full ? (
          <div className="flex flex-wrap items-center gap-2 text-xs shrink-0">
            <button type="button" onClick={onExit} className="inline-flex items-center gap-1 px-2 py-1 rounded bg-cyan-600 text-slate-950 font-semibold hover:bg-cyan-500" data-full-exit title="Свернуть (F11 или Esc)"><Minimize2 size={12} />Свернуть</button>
            {header}
            {drawer ? <button type="button" onClick={() => setDrawerOpen((v) => !v)} className="ml-auto inline-flex items-center gap-1 px-2 py-1 rounded bg-slate-800 border border-slate-700 text-slate-200 hover:bg-slate-700" data-full-drawer-toggle>{drawerOpen ? <PanelRightClose size={12} /> : <PanelRightOpen size={12} />}{drawerTitle}</button> : null}
          </div>
        ) : null}
        <div className={full ? 'flex-1 min-h-0 overflow-auto' : ''}>{children}</div>
      </div>
      {full && drawer && drawerOpen ? <aside className="w-[380px] shrink-0 overflow-y-auto border-l border-slate-800 bg-slate-950 p-2" data-full-drawer>{drawer}</aside> : null}
    </div>
  );
};
