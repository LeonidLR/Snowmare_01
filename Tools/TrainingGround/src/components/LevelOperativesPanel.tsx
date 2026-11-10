import React, { useState } from 'react';
import { Swords, Shield, Backpack, ArrowUp, ArrowDown, X, Plus, AlertTriangle } from 'lucide-react';
import {
  AVAILABLE_WEAPONS, STANDARD_OPERATIVES, getKit, getKitOverrideCarry, getLoadouts,
  setKitOverrideCarry, setOperativeKit, setOperativeLoadout, validateKit, validateLoadouts,
} from '../utils/levelSettings';
import type { LevelConfig, OperativeKit } from '../types';

interface LevelOperativesPanelProps {
  config: LevelConfig;
  onChange: (next: LevelConfig) => void;
}

export const LevelOperativesPanel: React.FC<LevelOperativesPanelProps> = ({ config, onChange }) => {
  const [selectedOpKey, setSelectedOpKey] = useState<string>('commander');
  const [weaponToAdd, setWeaponToAdd] = useState<string>('m16');

  const loadouts = getLoadouts(config);
  const kit = getKit(config);
  const kitOverride = getKitOverrideCarry(config);

  const loadoutErrors = validateLoadouts(loadouts);
  const kitErrors = validateKit(kit);
  const allErrors = [...loadoutErrors, ...kitErrors];

  const currentOp = STANDARD_OPERATIVES.find((o) => o.key === selectedOpKey) || STANDARD_OPERATIVES[0];
  const customLoadout = loadouts[currentOp.key];
  const hasCustomLoadout = Array.isArray(customLoadout) && customLoadout.length > 0;
  const currentWeapons = hasCustomLoadout ? customLoadout : currentOp.defaultWeapons;

  const currentOpKit = kit[currentOp.key] || {};

  // Handlers for Weapons
  const handleToggleGlobalWeapons = (useGlobal: boolean) => {
    if (useGlobal) {
      onChange(setOperativeLoadout(config, currentOp.key, undefined) as LevelConfig);
    } else {
      onChange(setOperativeLoadout(config, currentOp.key, [...currentOp.defaultWeapons]) as LevelConfig);
    }
  };

  const handleMoveWeapon = (index: number, direction: 'up' | 'down') => {
    const list = [...currentWeapons];
    const targetIdx = direction === 'up' ? index - 1 : index + 1;
    if (targetIdx < 0 || targetIdx >= list.length) return;
    const temp = list[index];
    list[index] = list[targetIdx];
    list[targetIdx] = temp;
    onChange(setOperativeLoadout(config, currentOp.key, list) as LevelConfig);
  };

  const handleRemoveWeapon = (index: number) => {
    const list = currentWeapons.filter((_, i) => i !== index);
    onChange(setOperativeLoadout(config, currentOp.key, list.length > 0 ? list : undefined) as LevelConfig);
  };

  const handleAddWeapon = () => {
    if (!weaponToAdd) return;
    const list = [...currentWeapons, weaponToAdd];
    onChange(setOperativeLoadout(config, currentOp.key, list) as LevelConfig);
  };

  // Handlers for Kit
  const handleUpdateKitField = (field: 'grenades' | 'medkits' | 'heat_packs', valueStr: string) => {
    const val = valueStr === '' ? undefined : parseInt(valueStr, 10);
    const updated: OperativeKit = {
      ...currentOpKit,
      [field]: val !== undefined && !Number.isNaN(val) ? Math.max(0, val) : undefined,
    };
    if (updated[field] === undefined) delete updated[field];
    onChange(setOperativeKit(config, currentOp.key, Object.keys(updated).length > 0 ? updated : undefined) as LevelConfig);
  };

  const handleUpdateAmmoField = (weaponId: string, valueStr: string) => {
    const val = valueStr === '' ? undefined : parseInt(valueStr, 10);
    const existingAmmo = { ...(currentOpKit.ammo || {}) };
    if (val === undefined || Number.isNaN(val)) {
      delete existingAmmo[weaponId];
    } else {
      existingAmmo[weaponId] = Math.max(0, val);
    }
    const updated: OperativeKit = {
      ...currentOpKit,
      ammo: Object.keys(existingAmmo).length > 0 ? existingAmmo : undefined,
    };
    if (!updated.ammo) delete updated.ammo;
    onChange(setOperativeKit(config, currentOp.key, Object.keys(updated).length > 0 ? updated : undefined) as LevelConfig);
  };

  return (
    <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-lg">
      {/* Header */}
      <div className="flex flex-wrap items-center justify-between gap-4 mb-4 pb-3 border-b border-slate-800">
        <div className="flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-rose-500/20 text-rose-400 flex items-center justify-center border border-rose-500/30">
            <Swords className="w-4 h-4 text-rose-400" />
          </div>
          <div>
            <h3 className="text-sm font-bold text-slate-100 flex items-center gap-2">
              Вооружение и стартовое снаряжение уровня
              <span className="text-[10px] font-mono uppercase bg-rose-950 text-rose-300 border border-rose-800 px-2 py-0.5 rounded">
                loadouts / kit
              </span>
            </h3>
            <p className="text-xs text-slate-400">
              Индивидуальное оружие и припасы каждого бойца на этом уровне (действует в реальной игре)
            </p>
          </div>
        </div>

        {/* kit_override_carry checkbox */}
        <label
          className="flex items-center gap-2 bg-slate-950 px-3 py-1.5 rounded-lg border border-slate-800 cursor-pointer hover:border-slate-700 text-xs text-slate-200"
          title="kit_override_carry: при переходе из кампании стартовое снаряжение этого уровня заменяет перенесённые припасы"
        >
          <input
            type="checkbox"
            checked={kitOverride}
            onChange={(e) => onChange(setKitOverrideCarry(config, e.target.checked) as LevelConfig)}
            className="accent-rose-500 w-4 h-4"
          />
          <span className="font-semibold text-rose-200">Снаряжение уровня важнее перенесённого (kit_override_carry)</span>
        </label>
      </div>

      {/* Operative selector tabs */}
      <div className="flex items-center gap-2 mb-4">
        {STANDARD_OPERATIVES.map((op) => {
          const isSelected = op.key === selectedOpKey;
          const hasCustomW = Array.isArray(loadouts[op.key]) && loadouts[op.key].length > 0;
          const hasCustomK = kit[op.key] && Object.keys(kit[op.key]).length > 0;

          return (
            <button
              key={op.key}
              onClick={() => setSelectedOpKey(op.key)}
              className={`flex items-center gap-2 px-3 py-1.5 rounded-lg text-xs font-bold transition-all border ${
                isSelected
                  ? 'bg-rose-500/20 text-rose-200 border-rose-500/50 shadow-sm'
                  : 'bg-slate-950/60 text-slate-400 border-slate-800 hover:text-slate-200 hover:bg-slate-800/40'
              }`}
            >
              <Shield className="w-3.5 h-3.5" />
              <span>{op.role}</span>
              {(hasCustomW || hasCustomK) && (
                <span className="w-1.5 h-1.5 rounded-full bg-rose-400" title="Настроено для уровня" />
              )}
            </button>
          );
        })}
      </div>

      {/* Content for Selected Operative */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Left Column: Weapons (loadouts) */}
        <div className="bg-slate-950 p-4 rounded-xl border border-slate-800 flex flex-col gap-3">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <Swords className="w-4 h-4 text-amber-400" />
              <h4 className="text-xs font-bold text-slate-200">
                Оружие: {currentOp.role} <span className="text-[10px] text-slate-500">({currentOp.key})</span>
              </h4>
            </div>

            <label className="flex items-center gap-1.5 text-xs text-slate-400 cursor-pointer">
              <input
                type="checkbox"
                checked={!hasCustomLoadout}
                onChange={(e) => handleToggleGlobalWeapons(e.target.checked)}
                className="accent-slate-500"
              />
              <span>По умолчанию (глобальное)</span>
            </label>
          </div>

          {/* Weapon list */}
          <div className="flex flex-col gap-2 min-h-[120px] bg-slate-900/50 p-3 rounded-lg border border-slate-800/60">
            {currentWeapons.map((weaponId, idx) => {
              const weaponMeta = AVAILABLE_WEAPONS.find((w) => w.id === weaponId);
              const weaponName = weaponMeta ? weaponMeta.name : weaponId;
              const isPrimary = idx === 0;

              return (
                <div
                  key={`${weaponId}_${idx}`}
                  className="flex items-center justify-between bg-slate-800/80 px-3 py-1.5 rounded border border-slate-700/60 text-xs"
                >
                  <div className="flex items-center gap-2">
                    <span className="font-mono text-slate-500 text-[10px] w-4">{idx + 1}.</span>
                    <span className="font-semibold text-slate-100">{weaponName}</span>
                    <span className="text-[10px] font-mono text-slate-500">({weaponId})</span>
                    {isPrimary && (
                      <span className="text-[9px] font-bold uppercase bg-amber-500/20 text-amber-300 border border-amber-500/30 px-1.5 py-0.2 rounded ml-1">
                        В руках (Primary)
                      </span>
                    )}
                  </div>

                  {hasCustomLoadout && (
                    <div className="flex items-center gap-1">
                      <button
                        onClick={() => handleMoveWeapon(idx, 'up')}
                        disabled={idx === 0}
                        title="Поднять выше (сделать основным)"
                        className="p-1 text-slate-400 hover:text-slate-100 disabled:opacity-20"
                      >
                        <ArrowUp className="w-3.5 h-3.5" />
                      </button>
                      <button
                        onClick={() => handleMoveWeapon(idx, 'down')}
                        disabled={idx === currentWeapons.length - 1}
                        title="Опустить ниже"
                        className="p-1 text-slate-400 hover:text-slate-100 disabled:opacity-20"
                      >
                        <ArrowDown className="w-3.5 h-3.5" />
                      </button>
                      <button
                        onClick={() => handleRemoveWeapon(idx)}
                        title="Убрать оружие"
                        className="p-1 text-rose-400 hover:text-rose-200 hover:bg-rose-950 rounded ml-1"
                      >
                        <X className="w-3.5 h-3.5" />
                      </button>
                    </div>
                  )}
                </div>
              );
            })}

            {!hasCustomLoadout && (
              <p className="text-[11px] text-slate-500 italic mt-auto">
                Используется стандартный набор оружия роли из weapons_tuning.json. Снимите галочку «По умолчанию», чтобы задать порядок и состав для этого уровня.
              </p>
            )}
          </div>

          {/* Add weapon picker */}
          {hasCustomLoadout && (
            <div className="flex items-center gap-2 mt-1">
              <select
                value={weaponToAdd}
                onChange={(e) => setWeaponToAdd(e.target.value)}
                className="bg-slate-800 border border-slate-700 rounded px-2.5 py-1 text-xs text-slate-200 focus:outline-none focus:border-rose-500 flex-1"
              >
                {AVAILABLE_WEAPONS.map((w) => (
                  <option key={w.id} value={w.id}>
                    {w.name} ({w.id})
                  </option>
                ))}
              </select>
              <button
                onClick={handleAddWeapon}
                className="flex items-center gap-1 bg-rose-600 hover:bg-rose-500 text-white px-3 py-1 rounded text-xs font-bold transition-all"
              >
                <Plus className="w-3.5 h-3.5" />
                Добавить
              </button>
            </div>
          )}
        </div>

        {/* Right Column: Starting Kit (kit) */}
        <div className="bg-slate-950 p-4 rounded-xl border border-slate-800 flex flex-col gap-3">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <Backpack className="w-4 h-4 text-emerald-400" />
              <h4 className="text-xs font-bold text-slate-200">
                Стартовый комплект (kit): {currentOp.role}
              </h4>
            </div>
            <span className="text-[11px] text-slate-500">пусто = по умолчанию</span>
          </div>

          {/* Supplies: Grenades, Medkits, Heat Packs */}
          <div className="grid grid-cols-3 gap-3">
            <label className="flex flex-col gap-1 text-xs">
              <span className="text-slate-400 font-medium">Гранаты</span>
              <input
                type="number"
                min={0}
                max={20}
                placeholder="по умолчанию"
                value={currentOpKit.grenades !== undefined ? currentOpKit.grenades : ''}
                onChange={(e) => handleUpdateKitField('grenades', e.target.value)}
                className="bg-slate-800 border border-slate-700 rounded px-2 py-1 text-xs text-slate-100 focus:outline-none focus:border-emerald-500"
              />
            </label>

            <label className="flex flex-col gap-1 text-xs">
              <span className="text-slate-400 font-medium">Аптечки</span>
              <input
                type="number"
                min={0}
                max={20}
                placeholder="по умолчанию"
                value={currentOpKit.medkits !== undefined ? currentOpKit.medkits : ''}
                onChange={(e) => handleUpdateKitField('medkits', e.target.value)}
                className="bg-slate-800 border border-slate-700 rounded px-2 py-1 text-xs text-slate-100 focus:outline-none focus:border-emerald-500"
              />
            </label>

            <label className="flex flex-col gap-1 text-xs">
              <span className="text-slate-400 font-medium">Теплопакеты</span>
              <input
                type="number"
                min={0}
                max={20}
                placeholder="по умолчанию"
                value={currentOpKit.heat_packs !== undefined ? currentOpKit.heat_packs : ''}
                onChange={(e) => handleUpdateKitField('heat_packs', e.target.value)}
                className="bg-slate-800 border border-slate-700 rounded px-2 py-1 text-xs text-slate-100 focus:outline-none focus:border-emerald-500"
              />
            </label>
          </div>

          {/* Ammo per weapon */}
          <div className="mt-2 pt-2 border-t border-slate-800/80">
            <span className="text-xs font-semibold text-slate-300 block mb-2">Запас патронов (ammo)</span>
            <div className="grid grid-cols-2 sm:grid-cols-3 gap-2">
              {['m16', 'pistol', 'shotgun', 'sniper_rifle', 'rpg7'].map((wId) => {
                const wMeta = AVAILABLE_WEAPONS.find((w) => w.id === wId);
                const count = currentOpKit.ammo ? currentOpKit.ammo[wId] : undefined;

                return (
                  <label key={wId} className="flex flex-col gap-0.5 text-xs bg-slate-900/60 p-2 rounded border border-slate-800">
                    <span className="text-[11px] text-slate-400">{wMeta?.name || wId}</span>
                    <input
                      type="number"
                      min={0}
                      max={999}
                      placeholder="по умолчанию"
                      value={count !== undefined ? count : ''}
                      onChange={(e) => handleUpdateAmmoField(wId, e.target.value)}
                      className="bg-slate-800 border border-slate-700 rounded px-1.5 py-0.5 text-xs text-slate-100 focus:outline-none focus:border-emerald-500 w-full"
                    />
                  </label>
                );
              })}
            </div>
          </div>

          <p className="text-[11px] text-slate-500 mt-1">
            Пустые поля не записываются в JSON и используют игровые значения по умолчанию для роли.
          </p>
        </div>
      </div>

      {allErrors.length > 0 && (
        <div className="mt-3 flex items-start gap-1.5 text-xs text-rose-300 bg-rose-950/40 p-2.5 rounded-lg border border-rose-800">
          <AlertTriangle className="w-4 h-4 mt-0.5 shrink-0" />
          <span>{allErrors.join('; ')}</span>
        </div>
      )}
    </div>
  );
};
