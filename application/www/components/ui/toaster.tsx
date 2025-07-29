"use client"

import {
  Toaster as ChakraToaster,
  Portal,
  Spinner,
  Stack,
  Toast,
  createToaster,
} from "@chakra-ui/react"

export const toaster = createToaster({
  placement: "top",
  pauseOnPageIdle: true,
  duration: 3000,
  // max: 1, // 限制最多显示一个toast，不限制，但是显示新的toast之前先把旧的删除
})

export const showToast = (options: Parameters<typeof toaster.create>[0]) => {
  toaster.dismiss(); // 先关闭所有toast
  return toaster.create(options);
}

export const Toaster = () => {
  return (
    <Portal>
      <ChakraToaster toaster={toaster} insetInline={{ mdDown: "4" }} >
        {(toast) => (
          <Toast.Root width={{ md: "sm" }}>
            {toast.type === "loading" ? (
              <Spinner size="sm" color="blue.solid" />
            ) : (
              <Toast.Indicator />
            )}
            <Stack gap="1" flex="1" maxWidth="100%"   >
              {toast.title && <Toast.Title fontSize="sm" >{toast.title}</Toast.Title>}
              {toast.description && (
                <Toast.Description fontSize="sm" >{toast.description}</Toast.Description>
              )}
            </Stack>
            {toast.action && (
              <Toast.ActionTrigger>{toast.action.label}</Toast.ActionTrigger>
            )}
            {toast.meta?.closable && <Toast.CloseTrigger />}
          </Toast.Root>
        )}
      </ChakraToaster>
    </Portal>
  )
}
