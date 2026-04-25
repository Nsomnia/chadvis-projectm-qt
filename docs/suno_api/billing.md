# Suno Billing & Credits API

Bearer JWT required for all endpoints below unless otherwise noted.

## Credits & Info

### `GET /api/billing/info/`
**Purpose**: Return current billing state, credits remaining, and plan metadata.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ total_credits_left, plan, plan_name, subscription_status, billing_period, currency, entitlement_flags, renewal_date?, cancellation_date? }`
**Notes**: Primary source of truth for credit balance. Used to gate generation, Studio access, and subscription prompts.

### `GET /api/billing/usage-plan-descriptions/`
**Purpose**: Return human-readable descriptions for available usage plans.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ plans: [{ id, name, description, credits?, studio_access?, features? }] }`
**Notes**: Descriptive payload intended for UI copy; often mirrors marketing text rather than raw entitlement data.

### `GET /api/billing/usage-plan-faq/`
**Purpose**: Return FAQ entries for plan and billing questions.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ faq: [{ question, answer, category? }] }`
**Notes**: Used by plan comparison and upgrade flows; content may be localized.

### `GET /api/billing/usage-plan-web-table-comparison/`
**Purpose**: Return the plan comparison table used on the web billing page.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ columns, rows, plans, comparison_items }`
**Notes**: Typically contains feature-by-plan matrix for Free, Pro, and Premier tiers.

### `GET /api/billing/usage-plans`
**Purpose**: List purchasable or active usage plans.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ plans: [{ id, name, price, currency, interval, credits, features, available }] }`
**Notes**: May include annual/monthly variants and plan availability flags.

### `GET /api/billing/default-currency`
**Purpose**: Return the account or region default currency.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ currency, symbol?, country?, locale? }`
**Notes**: Used to format checkout, invoices, and price displays.

### `GET /api/billing/eligible-discounts`
**Purpose**: Return discounts the account is eligible to receive.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ discounts: [{ id, code, amount_off?, percent_off?, reason, expires_at?, applies_to? }] }`
**Notes**: Eligibility can depend on churn risk, tenure, or promotional campaigns.

### `GET /api/billing/get-discount-offer`
**Purpose**: Return the currently targeted discount offer for the account.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ offer_id, code, amount_off?, percent_off?, headline?, details?, expires_at?, redemption_url? }`
**Notes**: Often personalized; may differ from the generic eligible-discounts list.

### `GET /api/billing/tax-info`
**Purpose**: Return tax and invoicing metadata for the account.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ country, region?, postal_code?, tax_id?, tax_rate?, tax_exempt?, reverse_charge?, billing_name?, billing_address? }`
**Notes**: Used before checkout or subscription changes to compute final charge.

### `GET /api/billing/clips/{clip_id}/download/`
**Purpose**: Return a download payload or download URL for a clip when billing rules allow it.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ clip_id, download_url?, audio_url?, stream_url?, gated, reason?, requires_paid_plan? }`
**Notes**: Billing-gated endpoint; free or out-of-entitlement accounts may receive a denial or a restricted payload.

### `GET /api/billing/purchase-info/{purchase_id}/`
**Purpose**: Return details for a specific purchase, invoice, or checkout result.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ purchase_id, status, amount, currency, plan_id, credits_added?, created_at, receipt_url?, invoice_url? }`
**Notes**: Useful for reconciling checkout completion and subscription activation.

## Subscription Management

### `POST /api/billing/create-session/`
**Purpose**: Create a checkout session for a subscription purchase or upgrade.
**Auth**: Bearer JWT
**Body**: `{ plan_id, interval?, coupon_code?, success_url?, cancel_url?, currency?, billing_country?, trial? }`
**Response**: `{ session_id, checkout_url, provider, expires_at?, client_secret? }`
**Notes**: Typically hands off to a payment provider. Some flows may return a redirect URL only.

### `POST /api/billing/change-plan/`
**Purpose**: Change the current subscription plan.
**Auth**: Bearer JWT
**Body**: `{ plan_id, proration?, effective_at?, coupon_code?, interval? }`
**Response**: `{ subscription_id, old_plan_id, new_plan_id, status, effective_at, proration_amount?, invoice_url? }`
**Notes**: Can be immediate or scheduled. May trigger invoice generation and credit recalculation.

### `POST /api/billing/cancel-sub/`
**Purpose**: Cancel the active subscription.
**Auth**: Bearer JWT
**Body**: `{ reason?, feedback?, cancel_at_period_end? }`
**Response**: `{ subscription_id, status, cancel_at_period_end, effective_cancel_date?, access_until? }`
**Notes**: Usually preserves access until the billing period ends unless immediate cancellation is allowed.

### `POST /api/billing/unpause-sub/`
**Purpose**: Resume a paused subscription.
**Auth**: Bearer JWT
**Body**: `{ subscription_id?, effective_at? }`
**Response**: `{ subscription_id, status, resumed_at, next_billing_date? }`
**Notes**: Reactivates billing and entitlement checks after a pause state.

### `POST /api/billing/pause-sub/`
**Purpose**: Pause the active subscription.
**Auth**: Bearer JWT
**Body**: `{ subscription_id?, pause_at?, resume_at?, reason? }`
**Response**: `{ subscription_id, status, paused_at, resume_at?, access_until? }`
**Notes**: Used for temporary suspension flows; may preserve some entitlements through the current cycle.

### `POST /api/billing/accept-sub-coupon/`
**Purpose**: Apply or accept a coupon code to the subscription.
**Auth**: Bearer JWT
**Body**: `{ coupon_code, subscription_id?, plan_id?, interval? }`
**Response**: `{ accepted, coupon_code, discount_amount?, percent_off?, expires_at?, subscription_id? }`
**Notes**: May validate one-time redemption, plan restrictions, or campaign eligibility.

### `POST /api/billing/set-default-payment-method/`
**Purpose**: Set the account’s default payment method.
**Auth**: Bearer JWT
**Body**: `{ payment_method_id, subscription_id?, default_for_future_charges? }`
**Response**: `{ success, default_payment_method_id, provider, updated_at }`
**Notes**: Usually updates billing profile state used for renewals and failed-charge recovery.

### `POST /api/billing/submit-survey/`
**Purpose**: Submit subscription churn or cancellation survey feedback.
**Auth**: Bearer JWT
**Body**: `{ subscription_id?, reason, subreason?, freeform_feedback?, rating? }`
**Response**: `{ success, survey_id, recorded_at }`
**Notes**: Often called during cancel flows; can influence discount offers or retention handling.

### `GET /api/billing/get-churn-survey-options`
**Purpose**: Return predefined churn survey reasons and follow-up options.
**Auth**: Bearer JWT
**Body**: None.
**Response**: `{ options: [{ id, label, description, suboptions? }] }`
**Notes**: Used to populate cancellation retention surveys.

## Plans

### Free
**Purpose**: Entry tier with limited credits.
**Auth**: N/A
**Body**: N/A
**Response**: N/A
**Notes**: Limited credits, no Studio access.

### Pro
**Purpose**: Paid tier with increased credits.
**Auth**: N/A
**Body**: N/A
**Response**: N/A
**Notes**: More credits than Free, but still no Studio access.

### Premier
**Purpose**: Highest documented consumer tier.
**Auth**: N/A
**Body**: N/A
**Response**: N/A
**Notes**: Full credits, Studio access, V5.5 Voice Cloning, and Custom Models (3 per user).

## Credit-Related Feature Flags

- `hide-credits-enabled`
- `hide-credits-for-subscribers-enabled`
- `out-of-credits-banner*`
- `free_*` (various free-credit flags)
- `can_buy_credit_top_up`
- `reward_credit`
- `refund_credit`
- `golden_ticket_reward_credit`
- `bypass_hook_feed_caches`
- `bypass_unified_feed_caches`
- `jail-fraudulent-accounts-tenure-limit-days`

## Credit Abuse Note

- User-reported client-side override path produced free credits.
- Reported to Suno; additional credits were granted as a reward.
- Related mechanisms observed in `/b-side/feature-flags` and `localStorage` poisoning of `statsig.cached.evaluations.*`.
