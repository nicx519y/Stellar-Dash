'use client';

import dynamic from 'next/dynamic';
import { Suspense, useEffect } from 'react';
import { usePathname } from 'next/navigation';
import { AdminShell } from './admin-shell';
import AdminLoading from '@/app/admin/loading';

const Accounts = dynamic(() => import('@/app/admin/users/page'), { loading: AdminLoading });
const ServiceTokens = dynamic(() => import('@/app/admin/service-tokens/page'), { loading: AdminLoading });
const Firmware = dynamic(() => import('@/app/admin/firmware/page'), { loading: AdminLoading });
const FirmwareDetail = dynamic(() => import('@/components/admin/firmware-detail-page'), { loading: AdminLoading });
const Resources = dynamic(() => import('@/app/admin/resources/page'), { loading: AdminLoading });
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
  const Content = pathname === '/admin/firmware/detail' ? FirmwareDetail
    : pathname === '/admin/firmware' ? Firmware
    : pathname === '/admin/service-tokens' ? ServiceTokens
    : pathname === '/admin/resources' ? Resources
    : pathname === '/admin/images' ? Images : Accounts;
  return <AdminShell><Suspense fallback={<AdminLoading />}><Content /></Suspense></AdminShell>;
}
