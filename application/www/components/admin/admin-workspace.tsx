'use client';

import dynamic from 'next/dynamic';
import { useEffect } from 'react';
import { usePathname } from 'next/navigation';
import { AdminShell } from './admin-shell';
import AdminLoading from '@/app/admin/loading';

const Accounts = dynamic(() => import('@/app/admin/users/page'), { loading: AdminLoading });
const Firmware = dynamic(() => import('@/app/admin/firmware/page'), { loading: AdminLoading });
const Images = dynamic(() => import('@/app/admin/images/page'), { loading: AdminLoading });

export function AdminWorkspace() {
  const pathname = usePathname().replace(/\/$/, '');
  useEffect(() => {
    const root = document.documentElement;
    const previous = root.style.scrollbarGutter;
    root.style.scrollbarGutter = 'stable';
    return () => { root.style.scrollbarGutter = previous; };
  }, []);
  // These are views of one persistent workspace. Native history updates the
  // Next pathname without fetching another route payload or replacing the shell.
  const Content = pathname === '/admin/firmware' ? Firmware
    : pathname === '/admin/images' ? Images : Accounts;
  return <AdminShell><Content /></AdminShell>;
}
